/**
 * @file llm_client.h
 * @brief Abstract interface cho mọi LLM backend trong EduSchedule-Agent.
 *
 * Thiết kế theo nguyên tắc:
 *  - Open/Closed  : Thêm backend mới (OpenAI, Gemini) mà không sửa AgentLoop.
 *  - LSP          : Mọi subclass đều thay thế được LLMClient mà không vỡ contract.
 *  - ISP          : Interface chỉ khai báo những gì AgentLoop thực sự cần.
 *  - DIP          : AgentLoop phụ thuộc vào abstraction, không phụ thuộc OllamaClient.
 *
 * @author  Võ Hoàng Nam
 * @project EduSchedule-Agent (OOP Assignment)
 * @std     C++20 (std::expected yêu cầu C++23, xem ghi chú)
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ─── std::expected (C++23) — fallback macro nếu compiler chưa hỗ trợ ─────────
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  include <expected>
#  define EDU_HAS_EXPECTED 1
#else
#  define EDU_HAS_EXPECTED 0
#endif

namespace edu_schedule {

// ══════════════════════════════════════════════════════════════════════════════
// § 1.  Error Types
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Phân loại lỗi của LLM client layer.
 *
 * Mỗi giá trị tương ứng với một nguyên nhân lỗi cụ thể, giúp tầng Agent
 * quyết định chiến lược retry hay escalate lên người dùng.
 */
enum class LLMErrorType : uint8_t {
    ConnectionRefused,  ///< Ollama server không chạy hoặc sai host/port
    Timeout,            ///< Vượt quá timeout_ms trước khi nhận response
    MalformedJson,      ///< Server trả về JSON không hợp lệ hoặc sai schema
    HttpError,          ///< HTTP status code != 200 (4xx/5xx)
    InvalidImage,       ///< File ảnh không tồn tại, rỗng, hoặc sai định dạng
};

/**
 * @brief Trả về mô tả chuỗi ngắn gọn cho LLMErrorType.
 * Dùng để in log / hiển thị cho người dùng.
 */
[[nodiscard]] constexpr std::string_view to_string(LLMErrorType err) noexcept
{
    switch (err) {
        case LLMErrorType::ConnectionRefused: return "ConnectionRefused";
        case LLMErrorType::Timeout:           return "Timeout";
        case LLMErrorType::MalformedJson:     return "MalformedJson";
        case LLMErrorType::HttpError:         return "HttpError";
        case LLMErrorType::InvalidImage:      return "InvalidImage";
    }
    return "Unknown";
}

// ══════════════════════════════════════════════════════════════════════════════
// § 2.  Data Structures
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Cấu hình khởi tạo cho bất kỳ LLM client nào.
 *
 * Được truyền vào constructor và có thể được cập nhật qua set_config().
 * Thiết kế Value Type (copyable, movable) để dễ dàng serialize/deserialize.
 */
struct LLMConfig {
    std::string base_url            = "http://localhost:11434"; ///< Host:Port của server LLM
    std::string model_name          = "qwen3-vl:7b";           ///< Tên model (vd: "gemma3:12b")
    float       temperature         = 0.7f;   ///< Độ sáng tạo [0.0 - 2.0]
    int         max_tokens          = 2048;   ///< Giới hạn token sinh ra (-1 = không giới hạn)
    long        timeout_ms          = 120000; ///< Timeout tổng (ms)
    long        connect_timeout_ms  = 10000;  ///< Timeout kết nối riêng (ms)
    bool        verify_ssl          = true;   ///< false khi dùng Ngrok/Colab self-signed cert
    bool        follow_redirect     = true;   ///< Theo HTTP redirect (Ngrok hay redirect)
};

/**
 * @brief Đại diện một tin nhắn trong lịch sử hội thoại (conversation history).
 *
 * Hỗ trợ cả text-only và multimodal trong cùng một struct.
 * Khi `base64_images` rỗng → request là text-only.
 * Khi `base64_images` có phần tử → request là multimodal (Vision).
 */
struct ChatMessage {
    std::string              role;           ///< "system" | "user" | "assistant"
    std::string              content;        ///< Nội dung văn bản
    std::vector<std::string> base64_images;  ///< Danh sách ảnh Base64 (để trống nếu text-only)

    // ── Static factory methods (Named Constructor Idiom) ────────────────────

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

/**
 * @brief Kết quả trả về từ một lần gọi LLM.
 *
 * Luôn được trả về (không throw) — caller kiểm tra `success` trước khi dùng
 * `content`. Các trường token/latency dùng cho tầng Harness và Trajectory.
 */
struct LLMResponse {
    // ── Kết quả chính ────────────────────────────────────────────────────────
    std::string content;              ///< Nội dung phản hồi (khi success = true)
    bool        success      = false; ///< true nếu gọi thành công

    // ── Thống kê token (cho Harness) ─────────────────────────────────────────
    int prompt_tokens     = 0; ///< prompt_eval_count từ Ollama
    int completion_tokens = 0; ///< eval_count từ Ollama
    int total_tokens      = 0; ///< prompt_tokens + completion_tokens

    // ── Thống kê thời gian (cho Trajectory) ──────────────────────────────────
    long long latency_ms           = 0; ///< Đo bằng std::chrono từ gửi đến nhận xong
    long long ollama_total_duration_ms = 0; ///< total_duration từ Ollama (ns → ms)

    // ── Thông tin lỗi (khi success = false) ──────────────────────────────────
    std::optional<LLMErrorType> error_type;    ///< Phân loại lỗi
    std::optional<std::string>  error_message; ///< Mô tả chi tiết lỗi
    int                         http_status = 0; ///< HTTP status code (0 nếu không kết nối được)

    // ── Thông tin bổ sung ─────────────────────────────────────────────────────
    std::string model_used;    ///< Tên model thực tế đã dùng
    bool        truncated = false; ///< true nếu done_reason = "length"
};

// ══════════════════════════════════════════════════════════════════════════════
// § 3.  LLMClient — Abstract Base Class (Interface)
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Interface thuần ảo cho tất cả LLM backend.
 *
 * ### LSP Contract (Liskov Substitution Principle):
 * Mọi subclass MỘT PHẢI tuân thủ:
 *  - `chat()` với messages rỗng phải trả về response có `success=false`.
 *  - `chat()` không được throw exception ra ngoài (bắt và đóng gói vào LLMResponse).
 *  - Khi `LLMConfig::timeout_ms` = 1ms, phải trả về `LLMErrorType::Timeout`.
 *
 * ### Cách sử dụng (AgentLoop):
 * ```cpp
 * std::unique_ptr<LLMClient> client = std::make_unique<OllamaClient>(config);
 * auto resp = client->chat({ChatMessage::user("Nhắc tôi nộp bài OOP")});
 * if (resp.success) { use(resp.content); }
 * ```
 */
class LLMClient {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    LLMClient()                            = default;
    virtual ~LLMClient()                   = default;

    // Non-copyable (handle tài nguyên bên trong không thể copy an toàn)
    LLMClient(const LLMClient&)            = delete;
    LLMClient& operator=(const LLMClient&) = delete;

    // Movable
    LLMClient(LLMClient&&)                 = default;
    LLMClient& operator=(LLMClient&&)      = default;

    // ── Core Interface (pure virtual) ─────────────────────────────────────────

    /**
     * @brief Gửi danh sách tin nhắn và nhận phản hồi đồng bộ (blocking).
     *
     * Đây là entry point chính. Hỗ trợ cả text-only và multimodal thông qua
     * cùng một interface — subclass tự xử lý sự khác biệt nội bộ.
     *
     * @param  messages Lịch sử hội thoại (có thể kèm ảnh trong ChatMessage).
     * @return          LLMResponse, luôn hợp lệ, không throw.
     */
    [[nodiscard]] virtual LLMResponse
    chat(const std::vector<ChatMessage>& messages) = 0;

    /**
     * @brief Overload tiện lợi: gửi một prompt đơn (text-only).
     *
     * Mặc định gọi chat() với một ChatMessage duy nhất.
     * Subclass có thể override để tối ưu nếu cần.
     */
    [[nodiscard]] virtual LLMResponse
    chat(std::string_view prompt)
    {
        return chat({ ChatMessage::user(std::string(prompt)) });
    }

    // ── Configuration ─────────────────────────────────────────────────────────

    virtual void set_config(const LLMConfig& config)     = 0;
    [[nodiscard]] virtual const LLMConfig& get_config() const noexcept = 0;

    /**
     * @brief Đổi model tạm thời (không tạo lại client).
     * Convenience wrapper gọi set_config() với model mới.
     */
    virtual void switch_model(std::string_view model_name)
    {
        LLMConfig cfg = get_config();
        cfg.model_name = std::string(model_name);
        set_config(cfg);
    }

    // ── Static Utilities ──────────────────────────────────────────────────────

    /**
     * @brief Đọc file ảnh nhị phân và trả về chuỗi Base64.
     *
     * Hàm static vì không phụ thuộc vào trạng thái của client.
     * Kết quả có thể dùng trực tiếp trong `ChatMessage::base64_images`.
     *
     * @param  path   Đường dẫn file (.png / .jpg / .jpeg)
     * @return        Base64 string, hoặc nullopt nếu file lỗi
     */
    [[nodiscard]] static std::optional<std::string>
    load_image_as_base64(const std::filesystem::path& path);

    /**
     * @brief Đọc nhiều file ảnh cùng lúc.
     * @param  paths Danh sách đường dẫn
     * @return       Cặp (danh sách base64 thành công, danh sách path thất bại)
     */
    [[nodiscard]] static std::pair<std::vector<std::string>, std::vector<std::filesystem::path>>
    load_images_as_base64(const std::vector<std::filesystem::path>& paths);

protected:
    /**
     * @brief Hàm helper tạo LLMResponse báo lỗi có cấu trúc.
     * Dùng bởi subclass trong catch block.
     */
    [[nodiscard]] static LLMResponse make_error(
        LLMErrorType        type,
        std::string         message,
        int                 http_status = 0,
        long long           latency_ms  = 0) noexcept
    {
        LLMResponse resp;
        resp.success       = false;
        resp.error_type    = type;
        resp.error_message = std::move(message);
        resp.http_status   = http_status;
        resp.latency_ms    = latency_ms;
        return resp;
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// § 4.  Factory Function (Dependency Injection helper)
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Factory tạo LLMClient theo tên backend.
 *
 * AgentLoop dùng hàm này thay vì include ollama_client.h trực tiếp → DIP.
 *
 * @param  backend  "ollama" | "openai" | "mock"
 * @param  config   Cấu hình khởi tạo
 * @return          unique_ptr<LLMClient>, nullptr nếu backend không được hỗ trợ
 */
[[nodiscard]] std::unique_ptr<LLMClient>
create_llm_client(std::string_view backend, const LLMConfig& config);

} // namespace edu_schedule
