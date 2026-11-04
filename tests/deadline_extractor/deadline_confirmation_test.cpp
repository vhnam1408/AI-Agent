#include "agent/deadline_confirmation.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace edu_schedule;
using Json = nlohmann::json;
namespace {

// Không dùng assert để điều kiện vẫn hoạt động khi Kaggle build Release.
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Fixture cố định theo đúng JSON sáu field TASK-017; không gọi model.
Json item_json(const std::string& text, const std::string& kind = "submission") {
    return {{"kind", kind}, {"course", "OOP"}, {"title", "Báo cáo"},
            {"date_text", text}, {"location", nullptr}, {"evidence", text}};
}

// Parse fixture qua API public và giữ nguyên source, không tự gán thời gian đã duyệt.
DeadlineExtraction extract_fixture(const std::string& text, const std::string& kind = "submission") {
    ExtractionRequest request{{"source-21", text, {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
    auto result = DeadlineExtractor::parse_response(
        Json{{"items", Json::array({item_json(text, kind)})}}.dump(), request);
    require(result.has_value(), "Fixture parse failed");
    return *result;
}

// Câu trả lời mặc định chỉ xác nhận thao tác; không có thành phần ngày giờ ngầm định.
DeadlineConfirmationAnswer reply(const DeadlineConfirmation& session) {
    const auto request = session.request();
    DeadlineConfirmationAnswer answer;
    answer.source_id = request.source_id;
    answer.item_index = request.item_index;
    answer.revision = request.revision;
    answer.explicit_confirmation = true;
    return answer;
}

// Mốc UTC cố định để so sánh kết quả chuyển đổi, không phụ thuộc đồng hồ/múi giờ máy.
TimePoint expected_deadline() {
    return std::chrono::sys_days{std::chrono::year{2026}/10/24} +
           std::chrono::hours{16} + std::chrono::minutes{59};
}

// Ngày giờ người dùng gửi đầy đủ; offset +07:00 được xác nhận tường minh.
ExtractedLocalDateTime complete_time() {
    ExtractedLocalDateTime when;
    when.year = 2026;
    when.month = 10;
    when.day = 24;
    when.hour = 23;
    when.minute = 59;
    when.utc_offset_minutes = 420;
    return when;
}

// Tìm mã issue trong request để kiểm policy chưa âm thầm loại cảnh báo.
bool has_issue(const DeadlineConfirmation& session, const std::string& code) {
    for (const auto& issue : session.request().issues) {
        if (issue.code == code) return true;
    }
    return false;
}

// Pending phải có câu hỏi/lý do và tuyệt đối không cấp TimePoint deadline.
void require_pending(const DeadlineConfirmation& session) {
    require(session.request().status == DeadlineConfirmationStatus::Pending, "Expected Pending");
    require(!session.deadline(), "Pending exposed a deadline");
    require(!session.request().issues.empty() && !session.request().questions.empty(),
            "Pending lost reasons/questions");
}

// Mock interface TASK-013 chỉ đếm lần gọi; confirmation không được gọi LLM lần nữa.
class MockLLMClient final : public LLMClient {
public:
    LLMConfig config;
    std::string content;
    int calls = 0;
    // Trả response cố định, chỉ dùng để kiểm extraction -> confirmation không tăng calls.
    LLMResult chat(const std::vector<ChatMessage>&) override {
        ++calls;
        LLMResponse response;
        response.content = content;
        return response;
    }
    // Lưu config như interface yêu cầu, không khởi tạo tài nguyên mạng/model.
    void set_config(const LLMConfig& value) override { config = value; }
    // Trả config đã lưu, không có side effect.
    const LLMConfig& get_config() const noexcept override { return config; }
};

} // namespace

// Chỉ chạy executable này trên Kaggle; ghi JSON từng ca và trả exit code khi thất bại.
int main(int argc, char** argv) {
    Json cases = Json::array();
    // Bắt lỗi từng ca để artifact chứa cả các ca sau một thất bại.
    const auto test = [&](const std::string& name, const std::function<void()>& body) {
        try {
            body();
            cases.push_back({{"name", name}, {"status", "PASS"}});
        } catch (const std::exception& error) {
            cases.push_back({{"name", name}, {"status", "FAIL"}, {"reason", error.what()}});
        }
    };
    const std::string complete = "Thứ 7 ngày 24/10/2026 nộp Báo cáo OOP lúc 23:59.";
    // Nguồn đã đủ dữ liệu phải giữ đúng UTC, không hỏi lại không cần thiết.
    test("valid_submission_preserves_utc", [&] {
        DeadlineConfirmation session(extract_fixture(complete), 0);
        require(session.request().status == DeadlineConfirmationStatus::NotRequired,
                "Valid source unnecessarily blocked");
        require(session.deadline() == expected_deadline(), "Wrong UTC");
    });
    const std::vector<std::pair<std::string, std::string>> pending_fixtures{
        {"missing_year", "Nộp Báo cáo OOP ngày 24/10 lúc 23:59."},
        {"missing_day", "Nộp Báo cáo OOP tháng 10/2026 lúc 23:59."},
        {"missing_month", "Nộp Báo cáo OOP ngày 24 năm 2026 lúc 23:59."},
        {"missing_time", "Nộp Báo cáo OOP ngày 24/10/2026."},
        {"impossible_date", "Nộp Báo cáo OOP ngày 31/02/2026 lúc 23:59."},
        {"weekday_mismatch", "Thứ 6 ngày 24/10/2026 nộp Báo cáo OOP lúc 23:59."},
        {"relative_week", "Nộp Báo cáo OOP tuần sau."},
        {"relative_month", "Nộp Báo cáo OOP cuối tháng."},
        {"abbreviated_year", "Nộp Báo cáo OOP ngày 24/10/26 lúc 23:59."},
        {"conflicting_dates", "Nộp Báo cáo OOP ngày 24/10/2026 hoặc 25/10/2026 lúc 23:59."},
        {"conflicting_times", "Nộp Báo cáo OOP ngày 24/10/2026 lúc 17:00 hoặc 23:59."},
        {"unsupported_timezone", "Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59 PST."},
        {"invalid_time", "Nộp Báo cáo OOP ngày 24/10/2026 lúc 24:01."}};
    for (const auto& [name, text] : pending_fixtures) {
        // Mỗi fixture thiếu/sai/mơ hồ phải giữ Pending cả sau một xác nhận không bổ sung dữ liệu.
        test(name, [&] {
            DeadlineConfirmation session(extract_fixture(text), 0);
            require_pending(session);
            const auto no_information = reply(session);
            require(session.answer(no_information).has_value(), "Explicit empty reply rejected");
            require_pending(session);
        });
    }
    // Không chọn ngày tuyệt đối khi cùng câu còn có lựa chọn thời gian tương đối.
    test("relative_text_alongside_absolute_date_is_not_auto_selected", [&] {
        DeadlineConfirmation session(extract_fixture(complete + " hoặc tuần sau."), 0);
        require_pending(session);
        require(has_issue(session, "ambiguous_relative_time"), "Relative ambiguity lost");
        auto answer = reply(session);
        answer.when = complete_time();
        require(session.answer(answer).has_value(), "Full reply rejected");
        require(session.deadline() == expected_deadline(), "Explicit resolution failed");
    });
    // Quote thiếu đầu/cuối dòng không được che lựa chọn tương đối hoặc ngày tuyệt đối khác.
    test("truncated_source_evidence_requires_full_timestamp_and_verification", [&] {
        for (const auto& source : {complete + " hoặc tuần sau.",
                                  std::string("Tuần sau hoặc ") + complete,
                                  complete + " hoặc ngày 25/10/2026 lúc 23:59."}) {
            ExtractionRequest input{{"truncated-21", source, {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
            auto result = DeadlineExtractor::parse_response(
                Json{{"items", Json::array({item_json(complete)})}}.dump(), input);
            require(result.has_value(), "Truncated fixture parse failed");
            const auto before = deadline_extraction_to_json(*result);
            DeadlineConfirmation session(*result, 0);
            require_pending(session);
            require(has_issue(session, "incomplete_source_evidence"), "Source truncation hidden");
            auto answer = reply(session);
            answer.when = complete_time();
            require(session.answer(answer).has_value(), "Timestamp reply rejected");
            require_pending(session);
            answer = reply(session);
            answer.evidence_verified = true;
            require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                    "Verified explicit selection failed");
            require(deadline_extraction_to_json(session.original()) == before, "Source snapshot changed");

            DeadlineConfirmation verification_only(*result, 0);
            answer = reply(verification_only);
            answer.evidence_verified = true;
            require(verification_only.answer(answer).has_value(), "Verification reply rejected");
            require_pending(verification_only);
        }
    });
    // Dòng không liên quan và khoảng trắng quanh quote không được gây hỏi lại giả.
    test("source_context_stays_on_quoted_lines_and_ignores_blank_margins", [&] {
        for (const auto& source : {std::string("Việc khác tuần sau.\r\n") + complete,
                                  std::string(" \t") + complete + " \t\r\nViệc khác tuần sau."}) {
            ExtractionRequest input{{"context-21", source, {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
            auto result = DeadlineExtractor::parse_response(
                Json{{"items", Json::array({item_json(complete)})}}.dump(), input);
            require(result.has_value(), "Context fixture parse failed");
            DeadlineConfirmation session(*result, 0);
            require(session.request().status == DeadlineConfirmationStatus::NotRequired &&
                    session.deadline() == expected_deadline(), "Unrelated context blocked valid source");
        }
    });
    // Quote giống nhau xuất hiện ở nhiều dòng phải kiểm hết, không chọn occurrence thuận lợi.
    test("repeated_quote_does_not_hide_ambiguous_occurrence", [&] {
        ExtractionRequest input{{"repeat-21", complete + "\n" + complete + " hoặc tuần sau.", {}},
                                std::nullopt, "Asia/Ho_Chi_Minh"};
        auto result = DeadlineExtractor::parse_response(
            Json{{"items", Json::array({item_json(complete)})}}.dump(), input);
        require(result.has_value(), "Repeated fixture parse failed");
        DeadlineConfirmation session(*result, 0);
        require_pending(session);
        require(has_issue(session, "incomplete_source_evidence") &&
                has_issue(session, "ambiguous_relative_time"), "Later ambiguity hidden");
    });
    // Bổ sung năm được xác nhận riêng, không ghi lại raw/evidence/validity gốc.
    test("missing_year_confirmation_preserves_original_and_provenance", [&] {
        auto extraction = extract_fixture("Nộp Báo cáo OOP ngày 24/10 lúc 23:59.");
        const auto before = deadline_extraction_to_json(extraction);
        DeadlineConfirmation session(extraction, 0);
        auto answer = reply(session);
        answer.when.year = 2026;
        require(session.answer(answer).has_value(), "Year rejected");
        require(session.request().status == DeadlineConfirmationStatus::Confirmed,
                "Not explicitly Confirmed");
        require(session.deadline() == expected_deadline(), "Wrong confirmed UTC");
        require(deadline_extraction_to_json(session.original()) == before, "Original overwritten");
        require(!session.original().items[0].deadline && session.answers().size() == 1,
                "Original validity/deadline changed");
        require(session.date_time().year_origin == "user_confirmed", "Provenance lost");
    });
    // Nhiều lượt xác nhận được tích lũy nhưng không tự điền giờ ở giữa các lượt.
    test("partial_replies_accumulate_without_filling_clock", [&] {
        DeadlineConfirmation session(extract_fixture("Nộp Báo cáo OOP tuần sau."), 0);
        auto answer = reply(session);
        answer.when.year = 2026;
        require(session.answer(answer).has_value(), "Year rejected");
        require_pending(session);
        require(!session.date_time().hour && !session.date_time().minute, "Invented midnight");
        answer = reply(session);
        answer.when = complete_time();
        require(session.answer(answer).has_value(), "Full reply rejected");
        require(session.deadline() == expected_deadline(), "Full reply not accepted");
    });
    // Sửa ngày không tồn tại tạo xác nhận mới, giữ nguyên item nguồn Invalid.
    test("full_reply_resolves_invalid_date_without_rewriting_invalid_source", [&] {
        DeadlineConfirmation session(extract_fixture("Nộp Báo cáo OOP ngày 31/02/2026 lúc 23:59."), 0);
        auto answer = reply(session);
        answer.when = complete_time();
        require(session.answer(answer).has_value(), "Corrected date rejected");
        require(session.deadline() == expected_deadline(), "Corrected date not confirmed");
        require(session.original().items[0].validity == ExtractionValidity::Invalid,
                "Original Invalid silently promoted");
    });
    // Chỉ đổi năm không được xóa cảnh báo thứ nếu ngày-thứ vẫn không khớp.
    test("wrong_weekday_remains_pending_until_explicitly_corrected", [&] {
        DeadlineConfirmation session(extract_fixture("Thứ 6 ngày 24/10/2026 nộp Báo cáo OOP lúc 23:59."), 0);
        auto answer = reply(session);
        answer.when.year = 2026;
        require(session.answer(answer).has_value(), "Reply rejected");
        require_pending(session);
        answer = reply(session);
        answer.when.weekday = 6;
        require(session.answer(answer).has_value(), "Correct weekday rejected");
        require(session.deadline() == expected_deadline(), "Weekday correction failed");
    });
    // Timestamp đầy đủ chưa đủ cho ảnh: phải có bước người dùng xác minh evidence.
    test("unverified_image_requires_human_verification", [&] {
        auto value = item_json(complete);
        ExtractionRequest input{{"image-21", "", {"AQID"}}, std::nullopt, "Asia/Ho_Chi_Minh"};
        auto extraction = DeadlineExtractor::parse_response(Json{{"items", Json::array({value})}}.dump(), input);
        require(extraction.has_value(), "Image fixture failed");
        DeadlineConfirmation session(*extraction, 0);
        require_pending(session);
        require(has_issue(session, "unverified_visual_evidence"), "Lost visual issue");
        auto answer = reply(session);
        answer.when = complete_time();
        require(session.answer(answer).has_value(), "Time reply rejected");
        require_pending(session);
        answer = reply(session);
        answer.evidence_verified = true;
        require(session.answer(answer).has_value(), "Verified image rejected");
        require(session.deadline() == expected_deadline(), "Verified image failed");
        require(session.original().source.base64_images == input.source.base64_images,
                "Original image modified");
        require(!session.original().items[0].deadline, "Image source got a fabricated deadline");
    });
    // Không xóa cảnh báo quote không có trong nguồn chỉ vì đã xác nhận thời gian.
    test("ungrounded_evidence_requires_separate_verification", [&] {
        ExtractionRequest input{{"evidence-21", "Chỉ có OOP.", {}}, std::nullopt, "UTC"};
        auto result = DeadlineExtractor::parse_response(
            Json{{"items", Json::array({item_json(complete)})}}.dump(), input);
        require(result.has_value(), "Fixture parse failed");
        DeadlineConfirmation session(*result, 0);
        auto answer = reply(session);
        answer.when = complete_time();
        require(session.answer(answer).has_value(), "Reply rejected");
        require_pending(session);
        require(has_issue(session, "ungrounded_evidence"), "Evidence issue silently cleared");
        answer = reply(session);
        answer.evidence_verified = true;
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Explicit evidence verification failed");
    });
    // Có/không có năm đều phải yêu cầu chọn occurrence tường minh, không tự duyệt occurrence khác.
    test("conflicting_items_require_full_time_and_explicit_selection", [&] {
        for (const bool year_present : {true, false}) {
            const std::string suffix = year_present ? "/2026" : "";
            const std::string first = "Nộp Báo cáo OOP ngày 24/10" + suffix + " lúc 23:59.";
            const std::string second = "Nộp Báo cáo OOP ngày 25/10" + suffix + " lúc 23:59.";
            ExtractionRequest input{{"conflict-21", first + "\n" + second, {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
            auto result = DeadlineExtractor::parse_response(
                Json{{"items", Json::array({item_json(first), item_json(second)})}}.dump(), input);
            require(result.has_value(), "Conflict parse failed");
            DeadlineConfirmation session(*result, 0);
            require_pending(session);
            require(has_issue(session, "conflicting_items"), "Cross-item conflict missed");
            auto answer = reply(session);
            answer.when = complete_time();
            require(session.answer(answer).has_value(), "Reply rejected");
            require_pending(session);
            answer = reply(session);
            answer.resolve_conflicts = true;
            require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                    "Explicit conflict selection failed");
            require(!session.original().items[1].deadline, "Other occurrence was silently approved");
        }
    });
    // Sai nguồn/item/revision hoặc chưa xác nhận không được thay đổi lịch sử/candidate.
    test("wrong_identity_stale_revision_and_implicit_reply_do_not_mutate", [&] {
        DeadlineConfirmation session(extract_fixture("Nộp Báo cáo OOP tuần sau."), 0);
        const auto valid = reply(session);
        auto answer = valid;
        answer.explicit_confirmation = false;
        require(!session.answer(answer), "Implicit confirmation accepted");
        answer = valid;
        answer.source_id = "wrong-source";
        require(!session.answer(answer), "Wrong source accepted");
        answer = valid;
        answer.item_index = 1;
        require(!session.answer(answer), "Wrong item accepted");
        answer = valid;
        answer.revision = 99;
        require(!session.answer(answer), "Wrong revision accepted");
        require(session.answers().empty(), "Rejected reply mutated history");
        answer = valid;
        answer.when.year = 2026;
        require(session.answer(answer).has_value(), "Valid reply rejected");
        require(!session.answer(valid), "Stale reply accepted");
        require(session.answers().size() == 1, "Rejected stale reply mutated history");
    });
    // Dữ liệu UI âm/quá lớn và lịch không tồn tại phải bị validator chung chặn.
    test("invalid_confirmation_ranges_and_calendar_never_create_timepoint", [&] {
        const std::vector<std::pair<std::string, int>> invalid{
            {"year", 26}, {"year", 1999}, {"year", 2100}, {"year", 2147483647},
            {"month", 0}, {"month", 13}, {"month", 257}, {"day", 0}, {"day", 32},
            {"day", 257}, {"hour", -1}, {"hour", 24}, {"minute", -1}, {"minute", 60},
            {"second", -1}, {"second", 60}, {"weekday", 0}, {"weekday", 8},
            {"timezone", -841}, {"timezone", 841}};
        for (const auto& [field, value] : invalid) {
            DeadlineConfirmation session(extract_fixture("Nộp Báo cáo OOP tuần sau."), 0);
            auto answer = reply(session);
            answer.when = complete_time();
            if (field == "year") answer.when.year = value;
            if (field == "month") answer.when.month = value;
            if (field == "day") answer.when.day = value;
            if (field == "hour") answer.when.hour = value;
            if (field == "minute") answer.when.minute = value;
            if (field == "second") answer.when.second = value;
            if (field == "weekday") answer.when.weekday = value;
            if (field == "timezone") answer.when.utc_offset_minutes = value;
            require(session.answer(answer).has_value(), "Invalid structured reply not recorded");
            require_pending(session);
        }
        auto when = complete_time();
        when.month = 2;
        when.day = 29;
        require(!DeadlineExtractor::validate_date_time(when).event_time, "Non-leap day accepted");
        when.year = 2028;
        require(DeadlineExtractor::validate_date_time(when).event_time.has_value(), "Leap day rejected");
    });
    // Phân biệt offset/giờ 0 tường minh với optional chưa có giá trị.
    test("missing_offset_no_named_timezone_fallback_and_zero_is_explicit", [&] {
        auto when = complete_time();
        when.utc_offset_minutes.reset();
        when.timezone = "Asia/Ho_Chi_Minh";
        require(!DeadlineExtractor::validate_date_time(when).event_time, "Implicit offset invented");
        when.utc_offset_minutes = 0;
        when.hour = 0;
        when.minute = 0;
        require(DeadlineExtractor::validate_date_time(when).event_time ==
                    std::chrono::sys_days{std::chrono::year{2026}/10/24}, "Explicit midnight/UTC rejected");
    });
    // Không coi offset mặc định còn lại sau lỗi parser là timezone đã được xác nhận.
    test("invalid_source_offset_does_not_become_a_valid_default_offset", [&] {
        DeadlineConfirmation session(extract_fixture("Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59 +99:99."), 0);
        require_pending(session);
        require(has_issue(session, "invalid_timezone"), "Invalid offset silently replaced by default");
        auto answer = reply(session);
        require(session.answer(answer).has_value(), "Reply rejected");
        require_pending(session);
        answer = reply(session);
        answer.when.utc_offset_minutes = 420;
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Explicit corrected offset failed");
    });
    // Metadata thiếu cần được bổ sung có nội dung và lưu riêng khỏi dữ liệu trích xuất.
    test("metadata_required_and_user_fields_kept_separately", [&] {
        auto json = item_json(complete);
        json["course"] = nullptr;
        json["title"] = nullptr;
        ExtractionRequest input{{"metadata-21", complete, {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
        auto result = DeadlineExtractor::parse_response(Json{{"items", Json::array({json})}}.dump(), input);
        require(result.has_value(), "Metadata fixture failed");
        DeadlineConfirmation session(*result, 0);
        auto answer = reply(session);
        answer.course = "  ";
        require(session.answer(answer).has_value(), "Blank reply not recorded");
        require_pending(session);
        answer = reply(session);
        answer.course = "OOP";
        answer.title = "Báo cáo";
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Explicit metadata not accepted");
        require(!session.original().items[0].course, "Original course overwritten");
    });
    // Item Submission không có marker nguồn cần người dùng xác nhận loại sự kiện.
    test("source_without_submission_marker_requires_explicit_verification", [&] {
        DeadlineConfirmation session(extract_fixture("Báo cáo OOP ngày 24/10/2026 lúc 23:59."), 0);
        require_pending(session);
        require(has_issue(session, "unsupported_submission"), "Missing submission guard");
        auto answer = reply(session);
        answer.submission_verified = true;
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Explicit submission verification failed");
    });
    for (const auto& kind : {"exam", "class", "assigned", "other"}) {
        // Xác nhận thời gian/cờ submission không được chuyển loại sự kiện không phải hạn nộp.
        test(std::string("non_submission_") + kind, [&] {
            DeadlineConfirmation session(extract_fixture(complete, kind), 0);
            require(session.request().status == DeadlineConfirmationStatus::NotApplicable,
                    "Non-submission requested a deadline");
            auto answer = reply(session);
            answer.when = complete_time();
            answer.submission_verified = true;
            require(!session.answer(answer) && !session.deadline(), "Non-submission promoted");
        });
    }
    // Sửa thành giờ sai phải thu hồi deadline cũ ngay, rồi kiểm lại khi sửa đúng.
    test("invalid_correction_revokes_previous_confirmed_deadline", [&] {
        DeadlineConfirmation session(extract_fixture(complete), 0);
        auto answer = reply(session);
        answer.when.hour = 99;
        require(session.answer(answer).has_value(), "Correction not recorded");
        require_pending(session);
        answer = reply(session);
        answer.when.hour = 23;
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Valid correction failed");
    });
    // Offset khác nhau chưa đủ chứng minh conflict; vẫn hỏi lựa chọn occurrence trước khi cấp deadline.
    test("partial_dates_with_different_offsets_are_not_forced_into_conflict", [&] {
        const std::string first = "Nộp Báo cáo OOP ngày 24/10 lúc 23:59 UTC+07:00.";
        const std::string second = "Nộp Báo cáo OOP ngày 24/10 lúc 16:59 UTC.";
        ExtractionRequest input{{"offset-21", first + "\n" + second, {}}, std::nullopt, "Asia/Ho_Chi_Minh"};
        auto result = DeadlineExtractor::parse_response(
            Json{{"items", Json::array({item_json(first), item_json(second)})}}.dump(), input);
        require(result.has_value(), "Offset fixture failed");
        DeadlineConfirmation session(*result, 0);
        require_pending(session);
        require(!has_issue(session, "conflicting_items"), "Equivalent partial times forced into conflict");
        auto answer = reply(session);
        answer.when.year = 2026;
        require(session.answer(answer).has_value(), "Year reply rejected");
        require_pending(session);
        require(has_issue(session, "unresolved_occurrences"), "Unnormalized alternatives silently selected");
        answer = reply(session);
        answer.when = complete_time();
        answer.resolve_conflicts = true;
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Explicit occurrence selection failed");
    });
    // Replay source/raw chặn cache đã bị sửa và từ chối snapshot/index không dùng được.
    test("replay_ignores_forged_cached_item_and_rejects_bad_raw_or_index", [&] {
        auto extraction = extract_fixture("Nộp Báo cáo OOP ngày 24/10 lúc 23:59.");
        extraction.items[0].when = complete_time();
        extraction.items[0].validity = ExtractionValidity::Valid;
        extraction.items[0].issues.clear();
        extraction.items[0].deadline = expected_deadline();
        DeadlineConfirmation session(extraction, 0);
        require_pending(session);
        bool rejected = false;
        try { DeadlineConfirmation bad(extraction, 99); }
        catch (const std::out_of_range&) { rejected = true; }
        require(rejected, "Out-of-range index accepted");
        extraction.raw_model_content = "{}";
        rejected = false;
        try { DeadlineConfirmation bad(extraction, 0); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Unreplayable extraction accepted");
    });
    // Luồng mock trích xuất chỉ gọi model một lần; phiên xác nhận hoàn toàn deterministic.
    test("mock_extraction_then_confirmation_does_not_call_llm_again", [&] {
        MockLLMClient mock;
        const std::string text = "Nộp Báo cáo OOP ngày 24/10 lúc 23:59.";
        mock.content = Json{{"items", Json::array({item_json(text)})}}.dump();
        DeadlineExtractor extractor(mock);
        auto extraction = extractor.extract({{"mock-21", text, {}}, std::nullopt, "Asia/Ho_Chi_Minh"});
        require(extraction.has_value() && mock.calls == 1, "Mock extraction failed");
        DeadlineConfirmation session(*extraction, 0);
        auto answer = reply(session);
        answer.when.year = 2026;
        require(session.answer(answer).has_value() && session.deadline() == expected_deadline(),
                "Confirmation failed");
        require(mock.calls == 1, "Confirmation called model");
    });
    int failed = 0;
    for (const auto& result : cases) if (result["status"] != "PASS") ++failed;
    const Json summary{{"passed", static_cast<int>(cases.size()) - failed},
                       {"failed", failed}, {"cases", cases}};
    std::cout << summary.dump(2) << '\n';
    if (argc >= 2) {
        std::ofstream output(argv[1]);
        output << summary.dump(2) << '\n';
        if (!output) return 2;
    }
    return failed == 0 ? 0 : 1;
}
