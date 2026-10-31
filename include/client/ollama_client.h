#pragma once

#include "client/llm_client.h"
#include "client/tool_chat.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

namespace edu_schedule {

/// Custom deleter cho CURL* easy handle
struct CurlEasyDeleter {
    // Giải phóng easy handle khi unique_ptr bị hủy; bỏ qua con trỏ null.
    void operator()(CURL* handle) const noexcept {
        if (handle) curl_easy_cleanup(handle);
    }
};

/// Custom deleter cho curl_slist* (danh sách HTTP headers)
struct CurlSlistDeleter {
    // Giải phóng toàn bộ danh sách header HTTP khi unique_ptr bị hủy.
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
    // Khởi tạo tài nguyên libcurl dùng chung; báo exception nếu khởi tạo thất bại.
    CurlGlobalGuard()
    {
        const CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) {
            throw std::runtime_error(
                std::string("curl_global_init failed: ") + curl_easy_strerror(rc));
        }
    }

    // Dọn tài nguyên libcurl dùng chung sau khi các Client đã được hủy.
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

    // Tạo Client bằng cấu hình mặc định và chuẩn bị HTTP handle.
    explicit OllamaClient();

    // Tạo Client với model, URL, timeout và tùy chọn sinh từ config.
    explicit OllamaClient(LLMConfig config);

    // Hủy Client; CurlHandle tự giải phóng HTTP handle thông qua deleter.
    ~OllamaClient() override = default;

    // Movable (CurlHandle là unique_ptr → movable)
    OllamaClient(OllamaClient&&)            = default;
    OllamaClient& operator=(OllamaClient&&) = default;

    // Gửi hội thoại text/ảnh đến Ollama và trả nội dung, metadata hoặc LLMError.
    [[nodiscard]] LLMResult
    chat(const std::vector<ChatMessage>& messages) override;

    using LLMClient::chat; // Cho phép gọi chat("Xin chao") trên OllamaClient.

    // Gửi messages cùng danh sách schema tools đến Ollama, kiểm tra và parse calls.
    // Trả metadata, calls và raw response; trả LLMError khi request/response lỗi.
    // Hàm chỉ nhận lời gọi từ model, không tự thực thi tool.
    [[nodiscard]] ToolChatResult chat_with_tools(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools);

    // Thay cấu hình dùng cho các request tiếp theo.
    void              set_config(const LLMConfig& config) override;
    // Trả tham chiếu chỉ đọc tới cấu hình hiện tại của Client.
    [[nodiscard]] const LLMConfig& get_config() const noexcept override;

private:

    // Ghép role, content, ảnh Base64 và tùy chọn model thành payload /api/chat.
    [[nodiscard]] nlohmann::json
    build_payload(const std::vector<ChatMessage>& messages) const;

    struct HttpResult {
        long        status_code = 0;
        std::string body;
        CURLcode    curl_code   = CURLE_OK;
    };

    // POST json_body tới url và thu body, HTTP status cùng mã lỗi libcurl.
    [[nodiscard]] HttpResult
    http_post(const std::string& url, const std::string& json_body);

    // Kiểm tra kết quả HTTP, giải mã response và gắn thời gian request vào metadata.
    [[nodiscard]] LLMResult
    parse_ollama_response(const HttpResult& result, long long latency_ms) const;

    // Chuyển mã lỗi libcurl sang loại lỗi của giao diện LLMClient.
    [[nodiscard]] static LLMErrorType map_curl_error(CURLcode code) noexcept;

    // Nhận từng khối response từ libcurl và nối vào chuỗi tại userdata.
    // Trả số byte đã nhận; trả 0 nếu không thể ghi để libcurl dừng truyền.
    static size_t write_callback(
        char*   ptr,
        size_t  size,
        size_t  nmemb,
        void*   userdata) noexcept;

    // Tạo header Content-Type/Accept JSON, bỏ trang cảnh báo ngrok và đặt User-Agent.
    [[nodiscard]] CurlSlist build_headers() const;

    LLMConfig  m_config;  ///< Cấu hình hiện tại

    CurlHandle m_curl;
};

} // namespace edu_schedule
