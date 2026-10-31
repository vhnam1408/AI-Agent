#include "client/tool_parser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace edu_schedule {
namespace {

using Json = nlohmann::json;
using ToolCallResult = std::expected<ToolCallAction, ParseError>;

constexpr std::string_view kWhitespace = " \t\r\n";
constexpr std::string_view kJsonFence = "```json\n";
constexpr std::string_view kPlainFence = "```\n";
constexpr std::string_view kClosingFence = "```";
constexpr std::size_t kMaxToolNameLength = 128;

enum class CallFormat {
    Content,
    Native,
};

/// Tạo nhánh lỗi cho std::expected, giữ mã lỗi và thông báo do parser cung cấp.
[[nodiscard]] std::unexpected<ParseError> make_error(
    ParseErrorCode code,
    std::string message)
{
    return std::unexpected(ParseError{code, std::move(message)});
}

/// Bỏ khoảng trắng ở hai đầu; view trả về vẫn tham chiếu chuỗi đầu vào.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }

    const auto last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

/// Bỏ một Markdown code fence bao trọn JSON; không tìm JSON giữa văn bản tự do.
[[nodiscard]] std::string_view unwrap_json_fence(std::string_view text) noexcept
{
    text = trim(text);
    for (const auto prefix : {kJsonFence, kPlainFence}) {
        if (text.starts_with(prefix) && text.ends_with(kClosingFence)) {
            return trim(text.substr(
                prefix.size(),
                text.size() - prefix.size() - kClosingFence.size()));
        }
    }

    return text;
}

/// Kiểm tra ký tự đầu tên tool: chữ cái ASCII hoặc dấu gạch dưới.
[[nodiscard]] constexpr bool is_name_start(char character) noexcept
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           character == '_';
}

/// Kiểm tra độ dài và cú pháp tên tool; các ký tự sau cho phép thêm số, '.' và '-'.
[[nodiscard]] bool is_valid_tool_name(std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMaxToolNameLength ||
        !is_name_start(name.front())) {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](char character) {
        return is_name_start(character) ||
               (character >= '0' && character <= '9') ||
               character == '.' || character == '-';
    });
}

/// Đọc một call theo định dạng content hoặc native và chuẩn hóa arguments thành JSON object.
/// known_tools chỉ kiểm tra tên đã biết, không cấp quyền thực thi tool.
[[nodiscard]] ToolCallResult parse_call(
    const Json& value,
    CallFormat format,
    std::span<const std::string> known_tools)
{
    if (!value.is_object()) {
        return make_error(ParseErrorCode::InvalidCall, "Tool call must be an object");
    }

    const bool is_native = format == CallFormat::Native;
    if (is_native && value.contains("type") && value["type"] != "function") {
        return make_error(ParseErrorCode::InvalidCall, "Only function calls are supported");
    }

    const Json* function = &value;
    if (is_native) {
        if (!value.contains("function") || !value["function"].is_object()) {
            return make_error(ParseErrorCode::InvalidCall, "Missing function object");
        }
        function = &value["function"];
    }

    const char* name_key = is_native ? "name" : "tool_name";
    if (!function->contains(name_key) || !(*function)[name_key].is_string()) {
        return make_error(ParseErrorCode::InvalidName, "Missing string function name");
    }

    const auto name = (*function)[name_key].get<std::string>();
    if (!is_valid_tool_name(name)) {
        return make_error(ParseErrorCode::InvalidName, "Invalid function name");
    }
    if (!known_tools.empty() &&
        std::find(known_tools.begin(), known_tools.end(), name) == known_tools.end()) {
        return make_error(ParseErrorCode::UnknownTool, "Unknown tool: " + name);
    }

    if (!function->contains("arguments")) {
        return make_error(ParseErrorCode::InvalidArguments, "Missing arguments");
    }

    Json arguments = (*function)["arguments"];
    if (arguments.is_string()) {
        arguments = Json::parse(arguments.get<std::string>(), nullptr, false);
    }
    if (!arguments.is_object()) {
        return make_error(
            ParseErrorCode::InvalidArguments,
            "Arguments must encode a JSON object");
    }

    return ToolCallAction{name, arguments.dump()};
}

/// Đọc mảng calls theo thứ tự; trả lỗi toàn bộ batch nếu bất kỳ phần tử nào không hợp lệ.
[[nodiscard]] ParseResult parse_calls(
    const Json& values,
    CallFormat format,
    std::span<const std::string> known_tools)
{
    if (!values.is_array()) {
        return make_error(ParseErrorCode::InvalidCall, "tool_calls must be an array");
    }

    ParsedMessage result;
    for (const auto& value : values) {
        auto parsed = parse_call(value, format, known_tools);
        if (!parsed) {
            // Không trả danh sách mới được kiểm tra một phần.
            return std::unexpected(parsed.error());
        }
        result.calls.push_back(std::move(*parsed));
    }

    return result;
}

/// Nhận diện trường gọi tool để phân biệt lệnh với JSON dữ liệu thông thường.
[[nodiscard]] bool contains_tool_calls(const Json& content)
{
    if (content.is_object()) {
        return content.contains("tool_name") || content.contains("tool_calls");
    }
    if (content.is_array()) {
        return std::any_of(content.begin(), content.end(), [](const Json& value) {
            return value.is_object() && value.contains("tool_name");
        });
    }

    return false;
}

} // namespace

/// Đọc lệnh JSON trong content: một call, mảng calls hoặc wrapper tool_calls.
/// Từ chối định dạng mơ hồ, danh sách rỗng và dữ liệu sai; không thực thi tool.
ParseResult parse_tool_content(
    std::string_view content,
    std::span<const std::string> known_tools)
{
    const auto document = Json::parse(unwrap_json_fence(content), nullptr, false);
    if (document.is_discarded()) {
        return make_error(ParseErrorCode::InvalidJson, "Invalid JSON tool content");
    }

    ParseResult result;
    if (document.is_object() && document.contains("tool_calls")) {
        if (document.contains("tool_name")) {
            return make_error(ParseErrorCode::AmbiguousCalls, "Mixed call formats");
        }
        result = parse_calls(document["tool_calls"], CallFormat::Native, known_tools);
    } else if (document.is_array()) {
        result = parse_calls(document, CallFormat::Content, known_tools);
    } else {
        auto parsed = parse_call(document, CallFormat::Content, known_tools);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        result = ParsedMessage{"", {std::move(*parsed)}};
    }

    if (!result) {
        return result;
    }
    if (result->calls.empty()) {
        return make_error(ParseErrorCode::InvalidCall, "Empty tool decision");
    }

    result->content = std::string(content);
    return result;
}

/// Đọc phản hồi /api/chat hoàn chỉnh, gồm content và native tool_calls.
/// Giữ câu trả lời thông thường; từ chối phản hồi thiếu, bị cắt hoặc chứa hai nguồn calls.
ParseResult parse_chat_response(
    std::string_view body,
    std::span<const std::string> known_tools)
{
    const auto document = Json::parse(body, nullptr, false);
    if (document.is_discarded()) {
        return make_error(ParseErrorCode::InvalidJson, "Invalid Ollama response JSON");
    }
    if (!document.is_object() || document.contains("error") ||
        !document.contains("message") || !document["message"].is_object()) {
        return make_error(
            ParseErrorCode::InvalidMessage,
            "Expected successful Ollama message object");
    }
    if ((document.contains("done") && document["done"] != true) ||
        (document.contains("done_reason") && document["done_reason"] == "length")) {
        return make_error(
            ParseErrorCode::IncompleteResponse,
            "Incomplete or truncated response");
    }

    const auto& message = document["message"];
    if (message.contains("role") && message["role"] != "assistant") {
        return make_error(ParseErrorCode::InvalidMessage, "Expected assistant role");
    }

    ParsedMessage result;
    if (message.contains("content")) {
        if (!message["content"].is_string()) {
            return make_error(ParseErrorCode::InvalidMessage, "content must be a string");
        }
        result.content = message["content"].get<std::string>();
    } else if (!message.contains("tool_calls")) {
        return make_error(ParseErrorCode::InvalidMessage, "Missing content and tool_calls");
    }

    if (message.contains("tool_calls")) {
        auto native_calls = parse_calls(message["tool_calls"], CallFormat::Native, known_tools);
        if (!native_calls) {
            return native_calls;
        }
        result.calls = std::move(native_calls->calls);
    }

    // Chỉ phân tích JSON khi content có dấu hiệu JSON; giữ nguyên câu trả lời thông thường.
    const auto text = unwrap_json_fence(result.content);
    if (text.empty() ||
        (text.front() != '{' && text.front() != '[' && !text.starts_with(kClosingFence))) {
        return result;
    }

    const auto content = Json::parse(text, nullptr, false);
    if (content.is_discarded()) {
        return make_error(ParseErrorCode::InvalidJson, "Malformed JSON message.content");
    }
    if (!contains_tool_calls(content)) {
        return result;
    }

    auto content_calls = parse_tool_content(result.content, known_tools);
    if (!content_calls) {
        return content_calls;
    }
    if (!result.calls.empty()) {
        return make_error(
            ParseErrorCode::AmbiguousCalls,
            "Both native and content tool calls");
    }

    result.calls = std::move(content_calls->calls);
    return result;
}

} // namespace edu_schedule
