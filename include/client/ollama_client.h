/**
 * @file ollama_client.h
 * @brief Concrete implementation của LLMClient cho Ollama REST API.
 *
 * OllamaClient biết cách:
 *  1. Xây dựng JSON payload chuẩn Ollama /api/chat (text-only + multimodal).
 *  2. Gửi HTTP POST bằng libcurl với RAII hoàn toàn.
 *  3. Parse response và ánh xạ lỗi vào LLMErrorType.
 *  4. Đo latency bằng std::chrono::steady_clock.
 *
 * @note Tầng AgentLoop KHÔNG nên include file này trực tiếp.
 *       Hãy dùng factory `create_llm_client("ollama", config)` từ llm_client.h.
 *
 * @author  Võ Hoàng Nam
 * @project EduSchedule-Agent
 * @std     C++20
 */

#pragma once

#include "client/llm_client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace edu_schedule {

// ══════════════════════════════════════════════════════════════════════════════
// § 1.  RAII Type Aliases for libcurl Resources
// ══════════════════════════════════════════════════════════════════════════════

/// Custom deleter cho CURL* easy handle
struct CurlEasyDeleter {
    void operator()(CURL* handle) const noexcept {
        if (handle) curl_easy_cleanup(handle);
    }
};

/// Custom deleter cho curl_slist* (danh sách HTTP headers)
struct CurlSlistDeleter {
    void operator()(curl_slist* list) const noexcept {
        if (list) curl_slist_free_all(list);
    }
};

/// RAII wrapper cho CURL easy handle — tự cleanup khi ra scope / throw exception
using CurlHandle = std::unique_ptr<CURL,       CurlEasyDeleter>;

/// RAII wrapper cho curl_slist headers — tự cleanup tương tự
using CurlSlist  = std::unique_ptr<curl_slist, CurlSlistDeleter>;

// ══════════════════════════════════════════════════════════════════════════════
// § 2.  CurlGlobalGuard — RAII cho curl_global_init/cleanup
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Singleton RAII guard cho libcurl global state.
 *
 * `curl_global_init()` phải được gọi một lần trước bất kỳ curl nào.
 * `curl_global_cleanup()` phải được gọi một lần khi chương trình thoát.
 *
 * Dùng:
 * ```cpp
 * int main() {
 *     CurlGlobalGuard curl_guard; // init
 *     // ... dùng curl ...
 * }  // → cleanup tự động
 * ```
 */
class CurlGlobalGuard {
public:
    CurlGlobalGuard()
    {
        const CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) {
            throw std::runtime_error(
                std::string("curl_global_init failed: ") + curl_easy_strerror(rc));
        }
    }

    ~CurlGlobalGuard() noexcept { curl_global_cleanup(); }

    // Non-copyable, non-movable (Singleton semantics)
    CurlGlobalGuard(const CurlGlobalGuard&)            = delete;
    CurlGlobalGuard& operator=(const CurlGlobalGuard&) = delete;
    CurlGlobalGuard(CurlGlobalGuard&&)                 = delete;
    CurlGlobalGuard& operator=(CurlGlobalGuard&&)      = delete;
};

// ══════════════════════════════════════════════════════════════════════════════
// § 3.  OllamaClient — Concrete LLMClient Implementation
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief LLM client cụ thể cho Ollama REST API.
 *
 * ### Thay thế bằng subclass khác (LSP):
 * ```cpp
 * // Thêm OpenAI sau mà không sửa gì ở AgentLoop:
 * class OpenAIClient : public LLMClient { ... };
 * ```
 *
 * ### Thread-safety:
 * Mỗi instance giữ một CURL handle riêng.
 * KHÔNG an toàn khi gọi chat() từ nhiều thread trên cùng một instance.
 * Dùng một OllamaClient per thread hoặc thêm mutex nếu cần.
 */
class OllamaClient final : public LLMClient {
public:
    // ── Constructors ──────────────────────────────────────────────────────────

    /**
     * @brief Khởi tạo với cấu hình mặc định.
     * @throws std::runtime_error nếu libcurl không khởi tạo được.
     */
    explicit OllamaClient();

    /**
     * @brief Khởi tạo với cấu hình tùy chỉnh.
     * @param config Cấu hình kết nối và model.
     */
    explicit OllamaClient(LLMConfig config);

    ~OllamaClient() override = default;

    // Movable (CurlHandle là unique_ptr → movable)
    OllamaClient(OllamaClient&&)            = default;
    OllamaClient& operator=(OllamaClient&&) = default;

    // ── LLMClient Interface Implementation ────────────────────────────────────

    /**
     * @brief Gửi hội thoại đến Ollama /api/chat và nhận phản hồi.
     *
     * Quy trình nội bộ:
     *  1. build_payload(messages) → nlohmann::json
     *  2. http_post(url, json_str) → (http_code, body)
     *  3. parse_ollama_response(body, http_code, latency_ms) → LLMResponse
     *
     * KHÔNG throw — mọi lỗi được đóng gói trong LLMResponse.
     */
    [[nodiscard]] LLMResponse
    chat(const std::vector<ChatMessage>& messages) override;

    // Kế thừa overload chat(string_view) từ LLMClient (gọi chat(vector))

    void              set_config(const LLMConfig& config) override;
    [[nodiscard]] const LLMConfig& get_config() const noexcept override;

private:
    // ── Private Helpers ───────────────────────────────────────────────────────

    /**
     * @brief Xây dựng JSON payload theo spec Ollama /api/chat.
     *
     * Tự động thêm trường `"images"` nếu bất kỳ message nào có base64_images.
     */
    [[nodiscard]] nlohmann::json
    build_payload(const std::vector<ChatMessage>& messages) const;

    /**
     * @brief Thực hiện HTTP POST bằng libcurl.
     *
     * @return {http_status_code, response_body}
     *         Nếu curl thất bại hoàn toàn (không connect được): http_code = 0.
     *
     * @note Hàm này có thể trả về LLMResponse lỗi thay vì pair khi gặp lỗi
     *       curl cấp thấp — dùng overload bên dưới.
     */
    struct HttpResult {
        long        status_code = 0;
        std::string body;
        CURLcode    curl_code   = CURLE_OK;
    };

    [[nodiscard]] HttpResult
    http_post(const std::string& url, const std::string& json_body);

    /**
     * @brief Parse JSON response từ Ollama thành LLMResponse.
     */
    [[nodiscard]] LLMResponse
    parse_ollama_response(const HttpResult& result, long long latency_ms) const;

    /**
     * @brief Ánh xạ CURLcode sang LLMErrorType.
     */
    [[nodiscard]] static LLMErrorType map_curl_error(CURLcode code) noexcept;

    /**
     * @brief libcurl write callback — tích lũy response body vào std::string.
     */
    static size_t write_callback(
        char*   ptr,
        size_t  size,
        size_t  nmemb,
        void*   userdata) noexcept;

    /**
     * @brief Xây dựng curl_slist headers và wrap vào CurlSlist (RAII).
     */
    [[nodiscard]] CurlSlist build_headers() const;

    // ── Member Variables ──────────────────────────────────────────────────────

    LLMConfig  m_config;  ///< Cấu hình hiện tại

    /**
     * @brief CURL easy handle được tái sử dụng giữa các lần gọi chat().
     *
     * Dùng `curl_easy_reset()` trước mỗi request để xóa option cũ.
     * unique_ptr đảm bảo cleanup ngay cả khi constructor throw.
     */
    CurlHandle m_curl;
};

} // namespace edu_schedule
