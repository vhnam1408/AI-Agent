#include "client/ollama_client.h"
#include <chrono>
#include <cmath>
#include <unordered_set>

namespace edu_schedule {
// Kiểm tra input, ghép schema tools vào payload text/ảnh, gửi HTTP và parse phản hồi.
// Kết quả giữ riêng raw response cùng metadata và calls để bên gọi kiểm tra hoặc thực thi.
ToolChatResult OllamaClient::chat_with_tools(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools) {
    // Chuyển lỗi cấu hình/input thành InvalidRequest trước khi gửi bất kỳ HTTP request nào.
    const auto invalid = [](std::string message) -> ToolChatResult {
        return std::unexpected(LLMError{LLMErrorType::InvalidRequest, std::move(message), 0, 0});
    };
    if (messages.empty() || tools.empty()) return invalid("Messages and tool definitions must be nonempty");
    if (m_config.model_name.find_first_not_of(" \t\r\n") == std::string::npos ||
        !(m_config.base_url.starts_with("http://") || m_config.base_url.starts_with("https://")) ||
        !std::isfinite(m_config.temperature) || m_config.temperature < 0 || m_config.temperature > 2 ||
        m_config.timeout_ms <= 0 || m_config.connect_timeout_ms <= 0) return invalid("Invalid Client configuration");
    try {
        std::vector<std::string> known;
        std::unordered_set<std::string> seen;
        auto payload = build_payload(messages); // Tạo payload từ role, content và ảnh Base64 hiện có.
        payload["tools"] = nlohmann::json::array();
        payload["think"] = false;
        for (const auto& tool : tools) {
            auto name = parse_tool_content(nlohmann::json{
                {"tool_name", tool.name}, {"arguments", nlohmann::json::object()}}.dump());
            if (!name || !seen.insert(tool.name).second || !tool.parameters.is_object() ||
                !tool.parameters.contains("type") || tool.parameters["type"] != "object") {
                return invalid("Invalid/duplicate tool definition: " + tool.name);
            }
            known.push_back(tool.name);
            payload["tools"].push_back({{"type", "function"}, {"function", {
                {"name", tool.name}, {"description", tool.description}, {"parameters", tool.parameters}}}});
        }
        std::string base = m_config.base_url;
        while (!base.empty() && base.back() == '/') base.pop_back();
        const auto start = std::chrono::steady_clock::now();
        auto transport = http_post(base.ends_with("/api/chat") ? base : base + "/api/chat", payload.dump());
        const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (transport.curl_code != CURLE_OK || transport.status_code != 200) {
            // Dùng bộ xử lý HTTP hiện có để giữ loại lỗi, status và thời gian request.
            auto result = parse_ollama_response(transport, latency);
            return std::unexpected(result.error());
        }
        auto parsed = parse_chat_response(transport.body, known);
        if (!parsed) return std::unexpected(LLMError{LLMErrorType::MalformedJson,
            parsed.error().message, static_cast<int>(transport.status_code), latency});

        // Bổ sung content đã parse vào bản sao response để kiểm tra metadata bằng
        // validator hiện có; response chỉ chứa native calls có thể thiếu content.
        // Giữ raw nguyên vẹn để bên gọi đối chiếu với dữ liệu model trả về.
        const auto raw = transport.body;
        auto metadata_body = nlohmann::json::parse(raw);
        metadata_body["message"]["content"] = parsed->content;
        transport.body = metadata_body.dump();
        auto metadata = parse_ollama_response(transport, latency);
        if (!metadata) return std::unexpected(metadata.error());
        return ToolChatResponse{std::move(*metadata), std::move(*parsed), raw};
    } catch (const nlohmann::json::exception& error) {
        return std::unexpected(LLMError{LLMErrorType::MalformedJson, error.what(), 0, 0});
    } catch (const std::runtime_error& error) {
        return std::unexpected(LLMError{LLMErrorType::NetworkError, error.what(), 0, 0});
    }
}
} // namespace edu_schedule
