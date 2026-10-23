#pragma once

#include "client/llm_client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

namespace edu_schedule {

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

// Tạo một guard trước các client. Destructor dọn curl sau khi các client bị hủy.
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

    // Không sao chép guard để tránh dọn cùng tài nguyên nhiều lần.
    CurlGlobalGuard(const CurlGlobalGuard&)            = delete;
    CurlGlobalGuard& operator=(const CurlGlobalGuard&) = delete;
    CurlGlobalGuard(CurlGlobalGuard&&)                 = delete;
    CurlGlobalGuard& operator=(CurlGlobalGuard&&)      = delete;
};

// Client cụ thể: chuyển tin nhắn sang JSON rồi gửi tới Ollama bằng HTTP.
class OllamaClient final : public LLMClient {
public:

    explicit OllamaClient();

    explicit OllamaClient(LLMConfig config);

    ~OllamaClient() override = default;

    // Movable (CurlHandle là unique_ptr → movable)
    OllamaClient(OllamaClient&&)            = default;
    OllamaClient& operator=(OllamaClient&&) = default;

    [[nodiscard]] LLMResult
    chat(const std::vector<ChatMessage>& messages) override;

    using LLMClient::chat; // Cho phép gọi chat("Xin chao") trên OllamaClient.

    void              set_config(const LLMConfig& config) override;
    [[nodiscard]] const LLMConfig& get_config() const noexcept override;

private:

    [[nodiscard]] nlohmann::json
    build_payload(const std::vector<ChatMessage>& messages) const;

    struct HttpResult {
        long        status_code = 0;
        std::string body;
        CURLcode    curl_code   = CURLE_OK;
    };

    [[nodiscard]] HttpResult
    http_post(const std::string& url, const std::string& json_body);

    [[nodiscard]] LLMResult
    parse_ollama_response(const HttpResult& result, long long latency_ms) const;

    [[nodiscard]] static LLMErrorType map_curl_error(CURLcode code) noexcept;

    static size_t write_callback(
        char*   ptr,
        size_t  size,
        size_t  nmemb,
        void*   userdata) noexcept;

    [[nodiscard]] CurlSlist build_headers() const;

    LLMConfig  m_config;  ///< Cấu hình hiện tại

    CurlHandle m_curl;
};

} // namespace edu_schedule
