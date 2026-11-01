#include "agent/deadline_extractor.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace edu_schedule;
using Json = nlohmann::json;
namespace {
// Exception thay assert để mọi điều kiện vẫn được kiểm tra trong Release.
void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read " + path);
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

// Mock chỉ hiện thực interface dùng chung, không cần Ollama hoặc repository.
class MockLLMClient final : public LLMClient {
public:
    LLMConfig config;
    LLMResponse response;
    std::optional<LLMError> failure;
    std::vector<ChatMessage> captured;
    int calls = 0;
    LLMResult chat(const std::vector<ChatMessage>& messages) override {
        ++calls;
        captured = messages;
        if (failure) {
            return std::unexpected(*failure);
        }
        return response;
    }
    void set_config(const LLMConfig& c) override {
        config = c;
    }
    const LLMConfig& get_config() const noexcept override {
        return config;
    }
};

Json make_item(std::string evidence, Json date = nullptr, std::string kind = "submission",
               Json course = "OOP", Json title = "Báo cáo", Json location = nullptr) {
    return {{"kind", kind},      {"course", course},     {"title", title},
            {"date_text", date}, {"location", location}, {"evidence", evidence}};
}
Json make_response(Json item) {
    return {{"items", Json::array({std::move(item)})}};
}
ExtractionRequest make_request(std::string text) {
    return {{"source-1", std::move(text), {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
}
DeadlineExtractionResult parse_result(const Json& value, const ExtractionRequest& source) {
    return DeadlineExtractor::parse_response(value.dump(), source);
}
bool has_issue(const ExtractedWorkItem& item, std::string_view code) {
    for (const auto& i : item.issues) {
        if (i.code == code) {
            return true;
        }
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    Json cases = Json::array();
    const auto test = [&](const std::string& name, const std::function<void()>& body) {
        try {
            body();
            cases.push_back({{"name", name}, {"status", "PASS"}});
        } catch (const std::exception& error) {
            cases.push_back({{"name", name}, {"status", "FAIL"}, {"reason", error.what()}});
        }
    };
    const std::string complete_source = "Thứ 7 ngày 24/10/2026 nộp Báo cáo OOP lúc 23:59.";
    const auto complete = make_response(make_item(complete_source, complete_source));
    const auto source = make_request(complete_source);
    test("complete_deadline_and_utc", [&] {
        auto result = parse_result(complete, source);
        require(result.has_value(), "Parse failed");
        auto json_result = deadline_extraction_to_json(*result);
        require(json_result["tasks"].size() == 1 &&
                    json_result["tasks"][0]["deadline_utc"] == "2026-10-24T16:59:00Z",
                "Wrong UTC deadline");
        require(result->items[0].validity == ExtractionValidity::Valid &&
                    result->items[0].when.weekday == 6,
                "Wrong status/weekday");
    });
    test("mock_interface_and_metadata", [&] {
        MockLLMClient mock;
        mock.response.content = complete.dump();
        mock.response.model_used = "mock";
        mock.response.http_status = 200;
        DeadlineExtractor extractor(mock);
        auto result = extractor.extract(source);
        require(result && mock.calls == 1 && mock.captured.size() == 2, "Mock not used once");
        require(mock.captured[0].role == "system" && mock.captured[1].role == "user",
                "Wrong roles");
        require(result->response_metadata && result->response_metadata->model_used == "mock",
                "Metadata lost");
        require(mock.captured[1].content.find(complete_source) != std::string::npos,
                "Source absent from request");
    });
    test("multiple_deadlines_and_courses", [&] {
        auto e2 = std::string("Nộp Bài lab Mạng ngày 25/10/2026 lúc 17:00.");
        auto doc = complete;
        doc["items"].push_back(make_item(e2, e2, "submission", "Mạng", "Bài lab"));
        auto result = parse_result(doc, make_request(complete_source + "\n" + e2));
        require(result && result->items.size() == 2, "Lost items");
        require(result->items[1].course == "Mạng" && result->items[1].deadline.has_value(),
                "Lost second course/deadline");
    });
    test("no_deadline", [&] {
        auto result = parse_result(Json{{"items", Json::array()}},
                                   make_request("Môn OOP chưa công bố hạn nộp."));
        require(result && deadline_extraction_to_json(*result)["tasks"].empty(), "Invented task");
    });
    test("missing_year_no_clock_fallback", [&] {
        const std::string source_text = "Thứ 6 ngày 24/10 nộp Báo cáo OOP lúc 23:59.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result.has_value(), "Parse failed");
        require(!result->items[0].when.year && !result->items[0].deadline &&
                    has_issue(result->items[0], "missing_field"),
                "Invented year");
    });
    test("explicit_year_context_2025", [&] {
        const std::string source_text = "Thứ 6 ngày 24/10 nộp Báo cáo OOP lúc 23:59.";
        auto source_request = make_request(source_text);
        source_request.reference_year = 2025;
        auto result =
            parse_result(make_response(make_item(source_text, source_text)), source_request);
        require(result && result->items[0].deadline &&
                    result->items[0].when.year_origin == "explicit_context",
                "Context year ignored");
    });
    test("weekday_conflict_with_2026_context", [&] {
        const std::string source_text = "Thứ 6 ngày 24/10 nộp Báo cáo OOP lúc 23:59.";
        auto source_request = make_request(source_text);
        source_request.reference_year = 2026;
        auto result =
            parse_result(make_response(make_item(source_text, source_text)), source_request);
        require(result && has_issue(result->items[0], "weekday_mismatch") &&
                    !result->items[0].deadline,
                "Weekday conflict ignored");
    });
    test("date_only_not_midnight", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 24/10/2026.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && result->items[0].when.local_date == "2026-10-24" &&
                    !result->items[0].when.hour && !result->items[0].deadline,
                "Invented midnight");
    });
    test("relative_date_stays_unresolved", [&] {
        const std::string source_text = "Tuần sau nộp Báo cáo OOP lúc 17:00.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && !result->items[0].when.day && !result->items[0].deadline,
                "Relative date inferred");
    });
    for (const auto& date : {"31/02/2026", "29/02/2025", "31/02", "24/13/2026", "24/10/26"}) {
        test(std::string("invalid_or_ambiguous_date_") + date, [&, date] {
            const std::string source_text =
                "Nộp Báo cáo OOP ngày " + std::string(date) + " lúc 23:59.";
            auto result = parse_result(make_response(make_item(source_text, source_text)),
                                       make_request(source_text));
            require(result && !result->items[0].deadline &&
                        result->items[0].validity == ExtractionValidity::Invalid,
                    "Invalid date accepted");
        });
    }
    test("leap_year_2024", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 29/02/2024 lúc 23:59.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && result->items[0].deadline.has_value(), "Leap date rejected");
    });
    test("iso_date_utc_rollover", [&] {
        const std::string source_text = "Nộp Báo cáo OOP 2026-01-01T01:00.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && deadline_extraction_to_json(*result)["tasks"][0]["deadline_utc"] ==
                              "2025-12-31T18:00:00Z",
                "Wrong rollover");
    });
    test("literal_vietnamese_date", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 24 tháng 10 năm 2026 lúc 23h59.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && result->items[0].deadline.has_value(), "Worded date failed");
    });
    for (const auto& kind : {"class", "exam", "assigned", "other"}) {
        test(std::string("not_submission_") + kind, [&, kind] {
            const std::string source_text = "Báo cáo OOP ngày 24/10/2026 lúc 23:59.";
            auto result = parse_result(make_response(make_item(source_text, source_text, kind)),
                                       make_request(source_text));
            require(result && !result->items[0].deadline && result->items[0].event_time &&
                        deadline_extraction_to_json(*result)["tasks"].empty(),
                    "Event converted to deadline");
        });
    }
    test("only_timetable_no_tasks", [&] {
        const std::string source_text = "Thứ 2 LT OOP tại E203 (CS2).";
        auto result = parse_result(
            make_response(make_item(source_text, nullptr, "class", "OOP", "LT", "E203 (CS2)")),
            make_request(source_text));
        require(result && result->items[0].kind == ExtractedEventKind::Class &&
                    !result->items[0].deadline &&
                    deadline_extraction_to_json(*result)["tasks"].empty(),
                "Class became task");
    });
    test("submission_marker_required", [&] {
        const std::string source_text = "Báo cáo OOP ngày 24/10/2026 lúc 23:59.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && has_issue(result->items[0], "unsupported_submission") &&
                    !result->items[0].deadline,
                "No hand-in evidence");
    });
    test("missing_course_title", [&] {
        const std::string source_text = "Nộp ngày 24/10/2026 lúc 23:59.";
        auto result = parse_result(
            make_response(make_item(source_text, source_text, "submission", nullptr, nullptr)),
            make_request(source_text));
        require(result && result->items[0].missing_fields.size() == 2 && !result->items[0].deadline,
                "Missing fields silently filled");
    });
    test("ungrounded_evidence", [&] {
        auto result = parse_result(complete, make_request("Không có thông báo này."));
        require(result && has_issue(result->items[0], "ungrounded_evidence") &&
                    !result->items[0].deadline,
                "Hallucinated evidence accepted");
    });
    test("invented_year_not_in_quote", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 24/10 lúc 23:59.";
        auto result = parse_result(make_response(make_item(source_text, "24/10/2026 23:59")),
                                   make_request(source_text));
        require(result && has_issue(result->items[0], "ungrounded_field") &&
                    !result->items[0].deadline,
                "Invented normalization accepted");
    });
    test("invented_course", [&] {
        auto json_result = complete;
        json_result["items"][0]["course"] = "AI";
        auto result = parse_result(json_result, source);
        require(result && has_issue(result->items[0], "ungrounded_field") &&
                    !result->items[0].deadline,
                "Invented course accepted");
    });
    test("images_forwarded_and_unverified_not_deadline", [&] {
        MockLLMClient mock;
        mock.response.content = complete.dump();
        DeadlineExtractor extractor(mock);
        auto source_request = make_request("");
        source_request.source.base64_images = {"QUJD", "REVG"};
        auto result = extractor.extract(source_request);
        require(result && mock.captured[1].base64_images == source_request.source.base64_images &&
                    result->source.base64_images == source_request.source.base64_images,
                "Images lost");
        require(has_issue(result->items[0], "unverified_visual_evidence") &&
                    !result->items[0].deadline,
                "Unverified vision silently approved");
    });
    test("evidence_and_source_owned", [&] {
        auto source_request = source;
        auto result = parse_result(complete, source_request);
        source_request.source.text.clear();
        require(result && result->source.text == complete_source &&
                    result->items[0].evidence == complete_source,
                "Source lost");
    });
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
             {"explicit_offset", "Nộp Báo cáo OOP 24/10/2026 23:59+09:00"},
             {"utc_word", "Nộp Báo cáo OOP 24/10/2026 23:59 UTC"},
             {"utc_z", "Nộp Báo cáo OOP 2026-10-24T23:59Z"}}) {
        test(pair.first, [&, pair] {
            auto result = parse_result(make_response(make_item(pair.second, pair.second)),
                                       make_request(pair.second));
            require(result.has_value(), "Parse failed");
            auto expected =
                pair.first == "explicit_offset" ? "2026-10-24T14:59:00Z" : "2026-10-24T23:59:00Z";
            require(deadline_extraction_to_json(*result)["tasks"][0]["deadline_utc"] == expected,
                    "Wrong offset");
        });
    }
    for (const auto& suffix : {"UTC+7", "+15:00", "PST", "23:00", "Asia/Tokyo"}) {
        test(std::string("timezone_or_time_conflict_") + suffix, [&, suffix] {
            auto source_text = complete_source + " " + suffix;
            auto result = parse_result(make_response(make_item(source_text, source_text)),
                                       make_request(source_text));
            require(result && !result->items[0].deadline &&
                        result->items[0].validity == ExtractionValidity::Invalid,
                    "Ambiguous/invalid clock accepted");
        });
    }
    for (const auto& clock : {"24:00", "12:61", "23:59:60", "23:59:12.5", "23:59 KST"}) {
        test(std::string("invalid_or_unsupported_clock_") + clock, [&, clock] {
            const std::string source_text =
                "Nộp Báo cáo OOP ngày 24/10/2026 lúc " + std::string(clock) + ".";
            auto result = parse_result(make_response(make_item(source_text, source_text)),
                                       make_request(source_text));
            require(result && !result->items[0].deadline &&
                        result->items[0].validity == ExtractionValidity::Invalid,
                    "Invalid clock/zone accepted");
        });
    }
    test("worded_weekday_mismatch", [&] {
        const std::string source_text = "Thứ Sáu ngày 24/10/2026 nộp Báo cáo OOP lúc 23:59.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && has_issue(result->items[0], "weekday_mismatch"),
                "Worded weekday ignored");
    });
    test("invalid_printed_weekday", [&] {
        const std::string source_text = "Thứ 8 ngày 24/10/2026 nộp Báo cáo OOP lúc 23:59.";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && has_issue(result->items[0], "invalid_weekday") &&
                    !result->items[0].deadline,
                "Invalid weekday ignored");
    });
    test("unsupported_default_timezone", [&] {
        auto source_request = source;
        source_request.default_timezone = "America/New_York";
        auto result = parse_result(complete, source_request);
        require(result && has_issue(result->items[0], "unsupported_timezone") &&
                    !result->items[0].deadline,
                "Host timezone fallback");
    });
    test("different_dates_in_same_quote", [&] {
        auto source_text = complete_source + " hoặc 25/10/2026";
        auto result = parse_result(make_response(make_item(source_text, source_text)),
                                   make_request(source_text));
        require(result && has_issue(result->items[0], "conflicting_dates") &&
                    !result->items[0].deadline,
                "Date choice invented");
    });
    test("same_work_conflicting_occurrences", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 25/10/2026 lúc 23:59.";
        auto response_json = complete;
        response_json["items"].push_back(make_item(source_text, source_text));
        auto result =
            parse_result(response_json, make_request(complete_source + "\n" + source_text));
        require(result && has_issue(result->items[0], "conflicting_items") &&
                    has_issue(result->items[1], "conflicting_items") &&
                    !result->items[0].deadline && !result->items[1].deadline,
                "Conflicting deadlines retained");
    });
    test("missing_clock_is_not_conflicting_clock", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 24/10/2026.";
        auto response_json = complete;
        response_json["items"].push_back(make_item(source_text, source_text));
        auto result =
            parse_result(response_json, make_request(complete_source + "\n" + source_text));
        require(result && !has_issue(result->items[0], "conflicting_items") &&
                    result->items[0].deadline && !result->items[1].deadline,
                "Unknown clock treated as contradiction");
    });
    test("equivalent_explicit_offsets_not_conflicting", [&] {
        const std::string source_text = "Nộp Báo cáo OOP ngày 24/10/2026 lúc 16:59 UTC.";
        auto response_json = complete;
        response_json["items"].push_back(make_item(source_text, source_text));
        auto result =
            parse_result(response_json, make_request(complete_source + "\n" + source_text));
        require(result && result->items[0].deadline &&
                    result->items[0].deadline == result->items[1].deadline &&
                    !has_issue(result->items[0], "conflicting_items"),
                "Equivalent instants treated as conflict");
    });
    test("llm_error_preserved", [&] {
        MockLLMClient mock;
        mock.failure = LLMError{LLMErrorType::Timeout, "slow", 504, 30000};
        DeadlineExtractor extractor(mock);
        auto result = extractor.extract(source);
        require(!result && result.error().code == ExtractionErrorCode::LLMFailure &&
                    result.error().llm_error->http_status == 504,
                "LLM error lost");
    });
    test("truncated_response_rejected", [&] {
        MockLLMClient mock;
        mock.response.content = complete.dump();
        mock.response.truncated = true;
        DeadlineExtractor extractor(mock);
        auto result = extractor.extract(source);
        require(!result && result.error().code == ExtractionErrorCode::TruncatedResponse,
                "Truncation accepted");
    });
    for (const auto& body : {"{bad", "", "{\"items\":[],\"items\":[]}"}) {
        test(std::string("invalid_json_") + body, [&, body] {
            auto result = DeadlineExtractor::parse_response(body, source);
            require(!result && result.error().code == ExtractionErrorCode::InvalidJson,
                    "Bad JSON accepted");
        });
    }
    for (const auto& body : {"[]", "{\"items\":null}", "{\"items\":[{}]}",
                             "{\"tool_name\":\"task_lookup\",\"arguments\":{}}",
                             "{\"message\":{\"tool_calls\":[]}}", "{\"items\":[],\"extra\":1}"}) {
        test(std::string("invalid_schema_") + body, [&, body] {
            auto result = DeadlineExtractor::parse_response(body, source);
            require(!result && result.error().code == ExtractionErrorCode::InvalidSchema,
                    "Wrong schema/tool call accepted");
        });
    }
    for (const auto& key : {"kind", "course", "title", "date_text", "location", "evidence"}) {
        test(std::string("missing_key_") + key, [&, key] {
            auto response_json = complete;
            response_json["items"][0].erase(key);
            auto result = parse_result(response_json, source);
            require(!result && result.error().code == ExtractionErrorCode::InvalidSchema,
                    "Missing key accepted");
        });
    }
    test("wrong_field_type", [&] {
        auto response_json = complete;
        response_json["items"][0]["course"] = true;
        require(!parse_result(response_json, source), "Boolean course accepted");
    });
    test("unknown_kind", [&] {
        auto response_json = complete;
        response_json["items"][0]["kind"] = "deadline";
        require(!parse_result(response_json, source), "Unknown kind accepted");
    });
    test("empty_evidence", [&] {
        auto response_json = complete;
        response_json["items"][0]["evidence"] = "";
        require(!parse_result(response_json, source), "Empty quote accepted");
    });
    test("too_many_items", [&] {
        auto response_json = complete;
        for (int i = 0; i < 128; ++i) {
            response_json["items"].push_back(response_json["items"][0]);
        }
        require(!parse_result(response_json, source), "129 records accepted");
    });
    test("response_limit", [&] {
        auto result =
            DeadlineExtractor::parse_response(std::string(2 * 1024 * 1024 + 1, 'x'), source);
        require(!result, "Response over limit accepted");
    });
    test("empty_source_before_llm", [&] {
        MockLLMClient mock_client;
        DeadlineExtractor extractor(mock_client);
        auto result = extractor.extract(make_request(""));
        require(!result && mock_client.calls == 0, "Empty source reached model");
    });
    test("invalid_base64_before_llm", [&] {
        MockLLMClient mock_client;
        DeadlineExtractor extractor(mock_client);
        auto source_request = source;
        source_request.source.base64_images = {"bad!"};
        auto result = extractor.extract(source_request);
        require(!result && mock_client.calls == 0, "Bad image reached model");
    });
    test("invalid_year_before_llm", [&] {
        MockLLMClient mock_client;
        DeadlineExtractor extractor(mock_client);
        auto source_request = source;
        source_request.reference_year = 1999;
        auto result = extractor.extract(source_request);
        require(!result && mock_client.calls == 0, "Bad year reached model");
    });
    test("markdown_fence", [&] {
        auto result =
            DeadlineExtractor::parse_response("```json\r\n" + complete.dump() + "\r\n```", source);
        require(result && result->items[0].deadline.has_value(), "Fence rejected");
    });
    test("repository_input_fixture_without_year", [&] {
        require(argc >= 2, "Fixture directory argument required");
        auto text = read_file(std::string(argv[1]) + "/../../assets/input.txt");
        auto fixture = Json::parse(
            read_file(std::string(argv[1]) +
                      "/../../tests/fixtures/deadline_extractor/input_mock_response.json"));
        auto result = parse_result(fixture, make_request(text));
        require(result && result->items.size() == 4, "Repository sample lost");
        require(deadline_extraction_to_json(*result)["tasks"].size() == 2,
                "Exam/test misclassified as hand-in");
        for (const auto& i : result->items) {
            require(!i.when.year && !i.deadline, "Sample year invented");
        }
    });
    test("printed_time_cannot_be_dropped_from_date_text", [&] {
        auto response_json = complete;
        response_json["items"][0]["date_text"] = "24/10/2026";
        auto result = parse_result(response_json, source);
        require(result && has_issue(result->items[0], "omitted_source_time"),
                "Lost source clock not detected");
        require(result->items[0].validity == ExtractionValidity::Invalid &&
                    !result->items[0].deadline,
                "Incomplete extraction approved");
    });
    test("null_date_text_cannot_hide_printed_time", [&] {
        auto response_json = complete;
        response_json["items"][0]["date_text"] = nullptr;
        auto result = parse_result(response_json, source);
        require(result && has_issue(result->items[0], "omitted_source_time") &&
                    !result->items[0].deadline,
                "Printed clock treated as absent");
    });
    test("no_source_time_is_not_model_omission", [&] {
        const std::string text = "Nộp Báo cáo OOP ngày 24/10/2026.";
        auto result = parse_result(make_response(make_item(text, text)), make_request(text));
        require(result && !has_issue(result->items[0], "omitted_source_time") &&
                    !result->items[0].when.hour,
                "Invented source clock");
    });
    test("captured_model_response_requires_course_in_evidence", [&] {
        require(argc >= 2, "Fixture directory argument required");
        const auto repo = std::string(argv[1]) + "/../..";
        const auto captured = Json::parse(read_file(
            repo + "/tests/fixtures/deadline_extractor/regression/captured_text_complete.json"));
        auto result = DeadlineExtractor::parse_response(
            captured["raw_model_content"].get<std::string>(),
            make_request(read_file(repo + "/tests/fixtures/deadline_extractor/live_syllabus.txt")));
        require(result && result->items.size() == 4, "Captured items lost");
        for (const auto& item : result->items) {
            require(has_issue(item, "ungrounded_field") && !item.deadline,
                    "Incomplete source quote approved");
        }
    });
    test("captured_weekday_response_also_reports_lost_clocks", [&] {
        require(argc >= 2, "Fixture directory argument required");
        const auto repo = std::string(argv[1]) + "/../..";
        const auto captured = Json::parse(read_file(
            repo +
            "/tests/fixtures/deadline_extractor/regression/captured_text_weekday_conflict.json"));
        auto request = make_request(read_file(repo + "/assets/input.txt"));
        request.reference_year = 2026;
        auto result = DeadlineExtractor::parse_response(
            captured["raw_model_content"].get<std::string>(), request);
        require(result && result->items.size() == 4, "Captured events lost");
        for (std::size_t index = 0; index < 3; ++index) {
            require(has_issue(result->items[index], "omitted_source_time"),
                    "Printed clock loss hidden by weekday failure");
            require(has_issue(result->items[index], "weekday_mismatch"), "Weekday conflict lost");
        }
        require(!has_issue(result->items[3], "omitted_source_time"),
                "Clock invented for event with no printed time");
    });
    test("visual_weekday_context_can_be_kept_without_absolute_date", [&] {
        const std::string evidence = "Thứ 3\n[LT] OOP\nPhòng: E203 (CS2)";
        auto request = make_request("");
        request.source.base64_images = {"QUJD"};
        auto result = parse_result(
            make_response(make_item(evidence, nullptr, "class", "OOP", "LT", "E203 (CS2)")),
            request);
        require(result && result->items[0].when.weekday == 2, "Image column weekday lost");
        require(result->items[0].validity == ExtractionValidity::NeedsReview &&
                    !result->items[0].deadline,
                "Visual event silently approved");
    });
    test("online_submission_location_is_preserved", [&] {
        const std::string text = "Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59 trên hệ thống LMS.";
        auto result = parse_result(
            make_response(make_item(text, text, "submission", "OOP", "Báo cáo", "hệ thống LMS")),
            make_request(text));
        require(result && result->items[0].location == "hệ thống LMS", "Online destination lost");
        require(result->items[0].deadline.has_value(), "Valid online hand-in deadline rejected");
        const auto output = deadline_extraction_to_json(*result);
        require(output["tasks"][0]["location"] == "hệ thống LMS",
                "Online destination lost in JSON");
    });
    test("location_is_never_autofilled_after_model_response", [&] {
        const std::string text = "Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59 trên hệ thống LMS.";
        const auto model_output = make_response(make_item(text, text));
        auto result = parse_result(model_output, make_request(text));
        require(result && !result->items[0].location, "Parser silently inserted LMS");
        require(result->raw_model_content == model_output.dump(), "Raw model content was changed");
    });
    int passed = 0;
    for (const auto& c : cases) {
        if (c["status"] == "PASS") {
            ++passed;
        }
    }
    Json summary{
        {"passed", passed}, {"failed", static_cast<int>(cases.size()) - passed}, {"cases", cases}};
    std::cout << summary.dump(2) << '\n';
    if (argc >= 3) {
        std::ofstream output(argv[2]);
        output << summary.dump(2) << '\n';
        if (!output) {
            return 2;
        }
    }
    return passed == static_cast<int>(cases.size()) ? 0 : 1;
}
