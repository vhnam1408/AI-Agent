#pragma once
#include "client/llm_client.h"
#include "client/tool_parser.h"
#include <nlohmann/json.hpp>

namespace edu_schedule {
// Mô tả tên, công dụng và JSON Schema tham số của tool gửi cho model lựa chọn.
struct ToolDefinition {
    std::string name;
    std::string description;
    nlohmann::json parameters;
};

// Gộp metadata HTTP/model, lời gọi đã parse và nguyên văn response để đối chiếu.
struct ToolChatResponse {
    LLMResponse response;
    ParsedMessage parsed;
    std::string raw_response;
};
using ToolChatResult = std::expected<ToolChatResponse, LLMError>;
} // namespace edu_schedule
