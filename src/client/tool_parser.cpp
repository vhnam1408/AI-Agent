#include "client/tool_parser.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace edu_schedule {
namespace {
using Json = nlohmann::json;
using CallResult = std::expected<ParsedToolCall, ParseError>;
constexpr std::size_t kMaxBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxCalls = 64;

// Tạo nhánh lỗi expected từ mã lỗi và mô tả để các hàm parse trả lỗi thống nhất.
std::unexpected<ParseError> fail(ParseErrorCode code, std::string message) {
    return std::unexpected(ParseError{code, std::move(message)});
}

// Bỏ khoảng trắng hai đầu và trả view vào chuỗi gốc, không sao chép nội dung.
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// Gỡ cặp hàng rào Markdown bao toàn bộ JSON, hỗ trợ xuống dòng LF và CRLF.
std::string_view unwrap(std::string_view text) {
    text = trim(text);
    for (const auto prefix : {std::string_view("```json\n"), std::string_view("```\n"),
                              std::string_view("```json\r\n"), std::string_view("```\r\n")}) {
        if (text.starts_with(prefix) && text.ends_with("```")) {
            return trim(text.substr(prefix.size(), text.size() - prefix.size() - 3));
        }
    }
    return text;
}

// Parse JSON và theo dõi khóa ở từng object, kể cả object lồng trong arguments.
// Trả giá trị discarded khi JSON sai cú pháp hoặc có khóa trùng, không giữ đè giá trị.
Json read_json(std::string_view text) {
    bool duplicate = false;
    std::vector<std::unordered_set<std::string>> keys;
    const auto callback = [&](int, Json::parse_event_t event, Json& value) {
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key && !keys.empty()) {
            duplicate |= !keys.back().insert(value.get<std::string>()).second;
        }
        if (event == Json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
        return true;
    };
    auto result = Json::parse(text, callback, false);
    return duplicate ? Json(Json::value_t::discarded) : result;
}

// Kiểm tra ký tự đầu tên tool: chữ ASCII hoặc dấu gạch dưới.
bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Kiểm tra tên dài 1..128 ký tự; sau ký tự đầu cho phép thêm chữ số, dấu chấm và gạch ngang.
bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 128 || !name_start(name.front())) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return name_start(c) || (c >= '0' && c <= '9') || c == '.' || c == '-';
    });
}

// Parse một call; native chọn cấu trúc function.name/arguments thay cho tool_name/arguments.
// Kiểm tra tên theo known nếu có, chuẩn hóa arguments thành object JSON và giữ id/index.
// Trả lỗi ngay khi trường bắt buộc hoặc kiểu dữ liệu không hợp lệ.
CallResult parse_call(const Json& value, bool native, std::span<const std::string> known) {
    if (!value.is_object()) return fail(ParseErrorCode::InvalidCall, "Call must be an object");
    const Json* function = &value;
    if (native) {
        if ((value.contains("type") && value["type"] != "function") ||
            !value.contains("function") || !value["function"].is_object()) {
            return fail(ParseErrorCode::InvalidCall, "Expected native function object");
        }
        function = &value["function"];
    }
    const auto key = native ? "name" : "tool_name";
    if (!function->contains(key) || !(*function)[key].is_string()) {
        return fail(ParseErrorCode::InvalidName, "Missing string tool name");
    }
    ParsedToolCall call;
    call.tool_name = (*function)[key].get<std::string>();
    if (!valid_name(call.tool_name)) return fail(ParseErrorCode::InvalidName, "Invalid tool name");
    if (!known.empty() && std::find(known.begin(), known.end(), call.tool_name) == known.end()) {
        return fail(ParseErrorCode::UnknownTool, "Unknown tool: " + call.tool_name);
    }
    if (!function->contains("arguments")) return fail(ParseErrorCode::InvalidArguments, "Missing arguments");
    Json arguments = (*function)["arguments"];
    if (arguments.is_string()) arguments = read_json(arguments.get<std::string>());
    if (!arguments.is_object()) return fail(ParseErrorCode::InvalidArguments, "Arguments must encode a JSON object");
    call.arguments = arguments.dump();
    if (native && value.contains("id")) {
        if (!value["id"].is_string() || value["id"].get<std::string>().empty()) {
            return fail(ParseErrorCode::InvalidCall, "Call id must be a nonempty string");
        }
        call.id = value["id"].get<std::string>();
    }
    if (native && function->contains("index")) {
        const auto& index = (*function)["index"];
        if (!index.is_number_integer() || index < 0 ||
            index > std::numeric_limits<std::size_t>::max()) {
            return fail(ParseErrorCode::InvalidCall, "Call index must be a nonnegative integer");
        }
        call.index = index.get<std::size_t>();
    }
    return call;
}

// Parse tối đa 64 calls theo đúng thứ tự; một call lỗi làm cả batch trả lỗi.
ParseResult parse_calls(const Json& values, bool native, std::span<const std::string> known) {
    if (!values.is_array()) return fail(ParseErrorCode::InvalidCall, "tool_calls must be an array");
    if (values.size() > kMaxCalls) return fail(ParseErrorCode::LimitExceeded, "Too many tool calls");
    ParsedMessage result;
    for (const auto& value : values) {
        auto call = parse_call(value, native, known);
        if (!call) return std::unexpected(call.error());
        result.calls.push_back(std::move(*call));
    }
    return result;
}

// Nhận biết khóa tool_name/tool_calls để phân biệt quyết định gọi tool với JSON dữ liệu thường.
bool has_call_marker(const Json& value) {
    if (value.is_object()) return value.contains("tool_name") || value.contains("tool_calls");
    if (value.is_array()) {
        return std::any_of(value.begin(), value.end(), [](const Json& item) {
            return item.is_object() && (item.contains("tool_name") || item.contains("tool_calls"));
        });
    }
    return false;
}
} // namespace

// Chuyển content JSON sang danh sách calls, giữ nguyên content và từ chối quyết định rỗng.
ParseResult parse_tool_content(std::string_view content, std::span<const std::string> known_tools) {
    if (content.size() > kMaxBytes) return fail(ParseErrorCode::LimitExceeded, "Content exceeds 4 MiB");
    const auto value = read_json(unwrap(content));
    if (value.is_discarded()) return fail(ParseErrorCode::InvalidJson, "Malformed or duplicate-key JSON content");
    ParseResult result;
    if (value.is_object() && value.contains("tool_calls")) {
        if (value.contains("tool_name")) return fail(ParseErrorCode::AmbiguousCalls, "Mixed call formats");
        result = parse_calls(value["tool_calls"], true, known_tools);
    } else if (value.is_array()) {
        result = parse_calls(value, false, known_tools);
    } else {
        auto call = parse_call(value, false, known_tools);
        if (!call) return std::unexpected(call.error());
        ParsedMessage message;
        message.calls.push_back(std::move(*call));
        result = std::move(message);
    }
    if (!result) return result;
    if (result->calls.empty()) return fail(ParseErrorCode::InvalidCall, "Empty tool decision");
    result->content = std::string(content);
    return result;
}

// Kiểm tra envelope Ollama, lấy native calls hoặc calls trong content; giữ câu trả lời
// thông thường khi content không biểu diễn tool và giới hạn body ở 4 MiB.
ParseResult parse_chat_response(std::string_view body, std::span<const std::string> known_tools) {
    if (body.size() > kMaxBytes) return fail(ParseErrorCode::LimitExceeded, "Response exceeds 4 MiB");
    const auto document = read_json(body);
    if (document.is_discarded()) return fail(ParseErrorCode::InvalidJson, "Malformed or duplicate-key response JSON");
    if (!document.is_object() || document.contains("error") || !document.contains("message") ||
        !document["message"].is_object()) return fail(ParseErrorCode::InvalidMessage, "Expected successful message object");
    if ((document.contains("done") && document["done"] != true) ||
        (document.contains("done_reason") && document["done_reason"] == "length")) {
        return fail(ParseErrorCode::IncompleteResponse, "Incomplete or truncated response");
    }
    const auto& message = document["message"];
    if (message.contains("role") && message["role"] != "assistant") {
        return fail(ParseErrorCode::InvalidMessage, "Expected assistant role");
    }
    ParsedMessage result;
    if (message.contains("content")) {
        if (!message["content"].is_string()) return fail(ParseErrorCode::InvalidMessage, "content must be a string");
        result.content = message["content"].get<std::string>();
    }
    if (message.contains("tool_calls")) {
        auto calls = parse_calls(message["tool_calls"], true, known_tools);
        if (!calls) return calls;
        result.calls = std::move(calls->calls);
    }
    if (!message.contains("content") && result.calls.empty()) {
        return fail(ParseErrorCode::InvalidMessage, "Missing content and nonempty tool_calls");
    }
    const auto text = unwrap(result.content);
    if (text.empty() || (text.front() != '{' && text.front() != '[' && !text.starts_with("```"))) return result;
    const auto value = read_json(text);
    if (value.is_discarded()) return fail(ParseErrorCode::InvalidJson, "Malformed JSON message.content");
    if (!has_call_marker(value)) return result;
    auto content_calls = parse_tool_content(result.content, known_tools);
    if (!content_calls) return content_calls;
    if (!result.calls.empty()) return fail(ParseErrorCode::AmbiguousCalls, "Both native and content calls");
    result.calls = std::move(content_calls->calls);
    return result;
}
} // namespace edu_schedule
