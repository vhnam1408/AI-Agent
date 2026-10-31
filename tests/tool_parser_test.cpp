#include "client/tool_parser.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace edu_schedule;
using Json = nlohmann::json;
namespace {
// Báo lỗi test khi điều kiện sai; exception vẫn hoạt động khi build Release.
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
// Đọc toàn bộ fixture từ path; báo lỗi nếu không mở được file.
std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open fixture: " + path);
    return {std::istreambuf_iterator<char>(file), {}};
}
// Tạo native function call giả với tên và arguments tùy chọn để dùng lại trong test.
Json native(std::string name = "task_lookup", Json arguments = Json{{"task_id", "T1"}}) {
    return {{"function", {{"name", name}, {"arguments", arguments}}}};
}
// Bọc mảng calls trong message của phản hồi Ollama hoàn tất để kiểm thử parser.
Json envelope(Json calls) {
    return {{"done", true}, {"message", {{"role", "assistant"}, {"tool_calls", calls}}}};
}
}

// Chạy các ca parser; argv[1] là thư mục fixture, argv[2] tùy chọn là file báo cáo JSON.
// Trả 0 khi tất cả PASS, 1 khi có FAIL, 2 khi không ghi được báo cáo.
int main(int argc, char** argv) {
    Json cases = Json::array();
    const std::vector<std::string> known{"task_lookup", "calculator"};
    // Chạy body và thu kết quả theo tên; bắt exception để tiếp tục các test sau.
    const auto test = [&](std::string name, const std::function<void()>& body) {
        try { body(); cases.push_back({{"name", name}, {"status", "PASS"}}); }
        catch (const std::exception& error) { cases.push_back({{"name", name}, {"status", "FAIL"}, {"reason", error.what()}}); }
    };
    test("original_kaggle_native_response", [&] {
        require(argc >= 2, "Expected fixture directory argument");
        auto parsed = parse_chat_response(read_file(std::string(argv[1]) + "/native_raw.json"), known);
        require(parsed && parsed->calls.size() == 1 && parsed->content.empty(), "Wrong native result");
        const auto& call = parsed->calls.front();
        require(call.tool_name == "task_lookup" && Json::parse(call.arguments) == Json{{"task_id", "T1"}}, "Arguments lost");
        require(call.id == "call_d38iu8jd" && call.index == 0, "Qwen id/index lost");
    });
    test("multiple_calls_preserve_order_and_nested_arguments", [&] {
        auto calls = Json::array({native(), native("calculator", {{"expression", "6 * 7"}, {"options", {{"trace", false}}}})});
        auto result = parse_chat_response(envelope(calls).dump(), known);
        require(result && result->calls.size() == 2, "Batch dropped calls");
        require(result->calls[0].tool_name == "task_lookup" && result->calls[1].tool_name == "calculator", "Batch reordered");
        require(Json::parse(result->calls[1].arguments)["options"]["trace"] == false, "Nested value changed");
    });
    test("string_encoded_arguments", [&] {
        auto result = parse_chat_response(envelope(Json::array({native("task_lookup", R"({"task_id":"T1"})")})).dump());
        require(result && Json::parse(result->calls[0].arguments) == Json{{"task_id", "T1"}}, "String arguments not normalized");
    });
    test("content_and_native_normalize_identically", [&] {
        auto a = parse_tool_content(R"({"tool_name":"task_lookup","arguments":{"task_id":"T1"}})", known);
        auto b = parse_chat_response(envelope(Json::array({native()})).dump(), known);
        require(a && b && a->calls[0].arguments == b->calls[0].arguments, "Formats differ");
    });
    for (const auto& [name, body] : std::vector<std::pair<std::string, std::string>>{
        {"content_array", R"([{"tool_name":"task_lookup","arguments":{}},{"tool_name":"calculator","arguments":{}}])"},
        {"native_wrapper", R"({"tool_calls":[{"function":{"name":"task_lookup","arguments":{}}}]})"},
        {"json_fence_lf", "```json\n{\"tool_name\":\"task_lookup\",\"arguments\":{}}\n```"},
        {"json_fence_crlf", "```json\r\n{\"tool_name\":\"task_lookup\",\"arguments\":{}}\r\n```"},
        {"plain_fence", "```\n{\"tool_name\":\"task_lookup\",\"arguments\":{}}\n```"}}) {
        test(name, [&] { require(parse_tool_content(body, known).has_value(), "Valid content rejected"); });
    }
    for (const auto& [name, body] : std::vector<std::pair<std::string, std::string>>{
        {"empty_input", ""}, {"malformed_json", "{"}, {"trailing_text", "{} trailing"},
        {"null_call", "null"}, {"primitive_call", "17"}, {"empty_object", "{}"},
        {"empty_content_batch", "[]"}, {"missing_arguments", R"({"tool_name":"task_lookup"})"},
        {"empty_name", R"({"tool_name":"","arguments":{}})"},
        {"numeric_name", R"({"tool_name":17,"arguments":{}})"},
        {"path_name", R"({"tool_name":"../exec","arguments":{}})"},
        {"space_name", R"({"tool_name":"task lookup","arguments":{}})"},
        {"unknown_name", R"({"tool_name":"unknown","arguments":{}})"},
        {"null_arguments", R"({"tool_name":"task_lookup","arguments":null})"},
        {"array_arguments", R"({"tool_name":"task_lookup","arguments":[]})"},
        {"boolean_arguments", R"({"tool_name":"task_lookup","arguments":true})"},
        {"numeric_arguments", R"({"tool_name":"task_lookup","arguments":1})"},
        {"bad_encoded_arguments", R"({"tool_name":"task_lookup","arguments":"{"})"},
        {"encoded_array_arguments", R"({"tool_name":"task_lookup","arguments":"[]"})"},
        {"duplicate_names", R"({"tool_name":"task_lookup","tool_name":"calculator","arguments":{}})"},
        {"duplicate_argument_keys", R"({"tool_name":"task_lookup","arguments":{"task_id":"T1","task_id":"T2"}})"},
        {"duplicate_encoded_keys", R"({"tool_name":"task_lookup","arguments":"{\"task_id\":1,\"task_id\":2}"})"},
        {"mixed_wrapper", R"({"tool_name":"task_lookup","arguments":{},"tool_calls":[]})"},
        {"partial_batch", R"([{"tool_name":"task_lookup","arguments":{}},null])"}}) {
        test(name, [&] { require(!parse_tool_content(body, known), "Invalid content accepted"); });
    }
    for (const auto& [name, body] : std::vector<std::pair<std::string, std::string>>{
        {"array_envelope", "[]"}, {"null_envelope", "null"}, {"missing_message", "{}"},
        {"null_message", R"({"message":null})"}, {"missing_both_sources", R"({"message":{}})"},
        {"numeric_content", R"({"message":{"content":3}})"}, {"null_content", R"({"message":{"content":null}})"},
        {"wrong_role", R"({"message":{"role":"user","content":"hello"}})"},
        {"done_false", R"({"done":false,"message":{"content":"hello"}})"},
        {"done_wrong_type", R"({"done":"true","message":{"content":"hello"}})"},
        {"truncated", R"({"done_reason":"length","message":{"content":"hello"}})"},
        {"server_error", R"({"error":"model error","message":{"content":"hello"}})"},
        {"bad_content_json", R"({"message":{"content":"{bad"}})"},
        {"nonarray_native", R"({"message":{"tool_calls":{}}})"},
        {"missing_function", R"({"message":{"tool_calls":[{}]}})"},
        {"wrong_native_type", R"({"message":{"tool_calls":[{"type":"exec","function":{"name":"task_lookup","arguments":{}}}]}})"},
        {"numeric_id", R"({"message":{"tool_calls":[{"id":1,"function":{"name":"task_lookup","arguments":{}}}]}})"},
        {"negative_index", R"({"message":{"tool_calls":[{"function":{"index":-1,"name":"task_lookup","arguments":{}}}]}})"},
        {"float_index", R"({"message":{"tool_calls":[{"function":{"index":1.5,"name":"task_lookup","arguments":{}}}]}})"},
        {"empty_native_without_content", R"({"message":{"tool_calls":[]}})"}}) {
        test(name, [&] { require(!parse_chat_response(body, known), "Invalid response accepted"); });
    }
    test("ambiguous_native_and_content", [&] {
        auto body = envelope(Json::array({native()}));
        body["message"]["content"] = R"({"tool_name":"task_lookup","arguments":{}})";
        auto result = parse_chat_response(body.dump());
        require(!result && result.error().code == ParseErrorCode::AmbiguousCalls, "Ambiguity not classified");
    });
    for (const auto& text : {"A normal answer", "", "[{\"subject\":\"OOP\"}]", "{\"classes\":[]}"}) {
        test("preserve_final_data_" + std::string(text), [&] {
            auto result = parse_chat_response(Json{{"message", {{"content", text}, {"tool_calls", Json::array()}}}}.dump());
            require(result && result->calls.empty() && result->content == text, "Final data changed");
        });
    }
    test("result_owns_input_data", [&] {
        auto result = parse_chat_response(envelope(Json::array({native()})).dump());
        require(result && Json::parse(result->calls[0].arguments)["task_id"] == "T1", "Dangling result data");
    });
    test("call_limit", [&] {
        auto calls = Json::array(); for (int i = 0; i < 65; ++i) calls.push_back(native());
        auto result = parse_chat_response(envelope(calls).dump());
        require(!result && result.error().code == ParseErrorCode::LimitExceeded, "Call limit ignored");
    });
    test("body_limit", [&] {
        auto result = parse_chat_response(std::string(4 * 1024 * 1024 + 1, 'x'));
        require(!result && result.error().code == ParseErrorCode::LimitExceeded, "Byte limit ignored");
    });
    test("unknown_name_error_code", [&] {
        auto result = parse_chat_response(envelope(Json::array({native("unknown")})).dump(), known);
        require(!result && result.error().code == ParseErrorCode::UnknownTool, "Wrong unknown error code");
    });
    int passed = 0;
    for (const auto& item : cases) if (item["status"] == "PASS") ++passed;
    Json summary{{"passed", passed}, {"failed", cases.size() - static_cast<std::size_t>(passed)}, {"cases", cases}};
    std::cout << summary.dump(2) << '\n';
    if (argc >= 3) {
        std::ofstream output(argv[2]); output << summary.dump(2) << '\n';
        if (!output) return 2;
    }
    return passed == static_cast<int>(cases.size()) ? 0 : 1;
}
