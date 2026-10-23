#pragma once
#include <cstdint>
#include <expected>
#include <utility>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

enum class LLMErrorType : uint8_t {
    InvalidRequest,     ///< Đầu vào hoặc cấu hình không hợp lệ
    NetworkError,       ///< DNS, TLS hoặc lỗi truyền dữ liệu
    ConnectionRefused,  ///< Server/cổng không chấp nhận kết nối
    Timeout,            ///< Vượt quá timeout_ms trước khi nhận response
    MalformedJson,      ///< Server trả về JSON không hợp lệ hoặc sai schema
    HttpError,          ///< HTTP status code != 200 (4xx/5xx)
    InvalidImage,       ///< File ảnh không tồn tại, rỗng, hoặc sai định dạng
};

[[nodiscard]] constexpr std::string_view to_string(LLMErrorType err) noexcept
{
    switch (err) {
        case LLMErrorType::InvalidRequest:    return "InvalidRequest";
        case LLMErrorType::NetworkError:      return "NetworkError";
        case LLMErrorType::ConnectionRefused: return "ConnectionRefused";
        case LLMErrorType::Timeout:           return "Timeout";
        case LLMErrorType::MalformedJson:     return "MalformedJson";
        case LLMErrorType::HttpError:         return "HttpError";
        case LLMErrorType::InvalidImage:      return "InvalidImage";
    }
    return "Unknown";
}

// Cấu hình dùng chung cho client và Agent.
struct LLMConfig {
    std::string base_url            = "http://localhost:11434"; ///< Host:Port của server LLM
    std::string model_name          = "qwen3-vl:7b";           ///< Tên model (vd: "gemma3:12b")
    float       temperature         = 0.7f;   ///< Độ sáng tạo [0.0 - 2.0]
    int         max_tokens          = 2048;   ///< Số dương: giới hạn token; <= 0: dùng mặc định server
    long        timeout_ms          = 120000; ///< Timeout tổng (ms)
    long        connect_timeout_ms  = 10000;  ///< Timeout kết nối riêng (ms)
    bool        verify_ssl          = true;   ///< Xác minh chứng chỉ HTTPS, kể cả URL ngrok
    bool        follow_redirect     = true;   ///< Theo HTTP redirect (Ngrok hay redirect)
};

// Một tin nhắn gửi AI. Danh sách ảnh rỗng nghĩa là chỉ gửi văn bản.
struct ChatMessage {
    std::string              role;           ///< "system" | "user" | "assistant"
    std::string              content;        ///< Nội dung văn bản
    std::vector<std::string> base64_images;  ///< Danh sách ảnh Base64 (để trống nếu text-only)

    /// Tạo tin nhắn "system" (định hướng hành vi model)
    [[nodiscard]] static ChatMessage system(std::string content_str)
    {
        return { "system", std::move(content_str), {} };
    }

    /// Tạo tin nhắn "user" text-only
    [[nodiscard]] static ChatMessage user(std::string content_str)
    {
        return { "user", std::move(content_str), {} };
    }

    /// Tạo tin nhắn "user" kèm danh sách ảnh Base64 (multimodal)
    [[nodiscard]] static ChatMessage user_with_images(
        std::string              content_str,
        std::vector<std::string> images)
    {
        return { "user", std::move(content_str), std::move(images) };
    }

    /// Tạo tin nhắn "assistant" (dùng trong few-shot prompting)
    [[nodiscard]] static ChatMessage assistant(std::string content_str)
    {
        return { "assistant", std::move(content_str), {} };
    }

    /// Kiểm tra xem tin nhắn này có kèm ảnh không
    [[nodiscard]] bool is_multimodal() const noexcept
    {
        return !base64_images.empty();
    }
};

// Chỉ chứa dữ liệu khi gọi thành công. Lỗi được giữ riêng trong LLMError.
struct LLMResponse {
    std::string content;

    int prompt_tokens     = 0; ///< prompt_eval_count từ Ollama
    int completion_tokens = 0; ///< eval_count từ Ollama
    int total_tokens      = 0; ///< prompt_tokens + completion_tokens

    long long latency_ms           = 0; ///< Đo bằng std::chrono từ gửi đến nhận xong
    long long ollama_total_duration_ms = 0; ///< total_duration từ Ollama (ns → ms)

    int                         http_status = 0; ///< HTTP status code (0 nếu không kết nối được)

    std::string model_used;    ///< Tên model thực tế đã dùng
    bool        truncated = false; ///< true nếu done_reason = "length"
};

struct LLMError {
    LLMErrorType type;
    std::string message;
    int http_status = 0;    // 0 nếu chưa nhận được HTTP response.
    long long latency_ms = 0;
};

// Có kết quả thì đọc result->content; có lỗi thì đọc result.error().message.
using LLMResult = std::expected<LLMResponse, LLMError>;

// Giao diện: lớp con phải tự cài đặt chat(), set_config(), get_config().
class LLMClient {
public:

    LLMClient()                            = default;
    virtual ~LLMClient()                   = default;

    // Non-copyable (handle tài nguyên bên trong không thể copy an toàn)
    LLMClient(const LLMClient&)            = delete;
    LLMClient& operator=(const LLMClient&) = delete;

    // Movable
    LLMClient(LLMClient&&)                 = default;
    LLMClient& operator=(LLMClient&&)      = default;

    [[nodiscard]] virtual LLMResult
    chat(const std::vector<ChatMessage>& messages) = 0;

    [[nodiscard]] virtual LLMResult
    chat(std::string_view prompt)
    {
        return chat({ ChatMessage::user(std::string(prompt)) });
    }

    virtual void set_config(const LLMConfig& config)     = 0;
    [[nodiscard]] virtual const LLMConfig& get_config() const noexcept = 0;

    virtual void switch_model(std::string_view model_name)
    {
        LLMConfig cfg = get_config();
        cfg.model_name = std::string(model_name);
        set_config(cfg);
    }

    [[nodiscard]] static std::optional<std::string>
    load_image_as_base64(const std::filesystem::path& path);

    [[nodiscard]] static std::pair<std::vector<std::string>, std::vector<std::filesystem::path>>
    load_images_as_base64(const std::vector<std::filesystem::path>& paths);

protected:
    [[nodiscard]] static LLMResult make_error(
        LLMErrorType        type,
        std::string         message,
        int                 http_status = 0,
        long long           latency_ms  = 0)
    {
        return std::unexpected(LLMError{type, std::move(message), http_status, latency_ms});
    }
};

[[nodiscard]] std::unique_ptr<LLMClient>
create_llm_client(std::string_view backend, const LLMConfig& config);

} // namespace edu_schedule
