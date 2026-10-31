#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

enum class ParseErrorCode {
    InvalidJson, InvalidMessage, InvalidCall, InvalidName, UnknownTool,
    InvalidArguments, AmbiguousCalls, IncompleteResponse, LimitExceeded
};

struct ParseError {
    ParseErrorCode code;
    std::string message;
};

// Lưu một lời gọi tool đã kiểm tra; các chuỗi sở hữu dữ liệu để không phụ thuộc input.
struct ParsedToolCall {
    std::string tool_name;
    std::string arguments; // Object arguments được tuần tự hóa thành chuỗi JSON.
    std::string id;
    std::optional<std::size_t> index;
};

struct ParsedMessage {
    std::string content;
    std::vector<ParsedToolCall> calls;
};

using ParseResult = std::expected<ParsedMessage, ParseError>;

// Đọc lời gọi tool trong content JSON, mảng calls hoặc khối Markdown chứa JSON.
// known_tools rỗng: chỉ kiểm tra cú pháp tên; có phần tử: chỉ nhận tên trong danh sách.
// Trả ParsedMessage hoặc ParseError; không cấp quyền hay thực thi tool.
[[nodiscard]] ParseResult parse_tool_content(
    std::string_view content, std::span<const std::string> known_tools = {});

// Đọc body của một phản hồi /api/chat không streaming, lấy content và các calls.
// Hỗ trợ native message.tool_calls hoặc JSON trong content; từ chối hai nguồn calls
// cùng tồn tại, dữ liệu lỗi hoặc phản hồi báo chưa hoàn tất/bị cắt.
[[nodiscard]] ParseResult parse_chat_response(
    std::string_view body, std::span<const std::string> known_tools = {});

} // namespace edu_schedule
