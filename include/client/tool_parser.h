#pragma once

#include "agent/agent_loop.h"
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

enum class ParseErrorCode { InvalidJson, InvalidMessage, InvalidCall, InvalidName,
                            UnknownTool, InvalidArguments, AmbiguousCalls, IncompleteResponse };
struct ParseError {
    ParseErrorCode code;
    std::string message;
};
struct ParsedMessage {
    std::string content;
    std::vector<ToolCallAction> calls; // Reuse the existing Agent contract; never execute.
};
using ParseResult = std::expected<ParsedMessage, ParseError>;

/// Đọc một call {"tool_name":"...","arguments":{...}}, mảng calls hoặc wrapper
/// {"tool_calls":[{"function":{"name":"...","arguments":{...}}}]}.
/// arguments có thể là object hoặc chuỗi JSON mã hóa object.
/// known_tools rỗng chỉ kiểm tra cú pháp; parser không cấp quyền hay thực thi tool.
[[nodiscard]] ParseResult parse_tool_content(
    std::string_view content, std::span<const std::string> known_tools = {});

/// Đọc phản hồi /api/chat không streaming, gồm message.content và message.tool_calls.
/// Trả lỗi nếu cả content và native cùng chứa lệnh gọi tool để tránh xử lý trùng.
[[nodiscard]] ParseResult parse_chat_response(
    std::string_view body, std::span<const std::string> known_tools = {});

} // namespace edu_schedule
