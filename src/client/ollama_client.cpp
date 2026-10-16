/**
 * @file ollama_client.cpp
 * @brief Triển khai đầy đủ OllamaClient cho Ollama REST API.
 *
 * Bao gồm:
 *  - Base64 encoder (RFC 4648, không phụ thuộc thư viện ngoài)
 *  - LLMClient::load_image_as_base64() và load_images_as_base64()
 *  - create_llm_client() factory function
 *  - OllamaClient: build_payload, http_post (RAII libcurl), parse_ollama_response
 *
 * @author  Võ Hoàng Nam
 * @project EduSchedule-Agent
 * @std     C++20
 */

#include "client/ollama_client.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace edu_schedule {

// ══════════════════════════════════════════════════════════════════════════════
// § 1.  Base64 Encoder (RFC 4648 — Internal Implementation)
// ══════════════════════════════════════════════════════════════════════════════

namespace {

/**
 * @brief Mã hóa dải byte sang chuỗi Base64 chuẩn RFC 4648.
 *
 * Thuật toán:
 *  - Xử lý 3 byte input → 4 ký tự Base64 output.
 *  - Padding '=' khi số byte không chia hết cho 3.
 *
 * @param data  Con trỏ đến dữ liệu nhị phân.
 * @param len   Số byte cần mã hóa.
 * @return      Chuỗi Base64 (chỉ chứa ký tự [A-Za-z0-9+/=]).
 */
[[nodiscard]] std::string base64_encode(const uint8_t* data, std::size_t len)
{
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((len + 2) / 3) * 4); // Pre-allocate chính xác

    for (std::size_t i = 0; i < len; i += 3) {
        const uint8_t b0 =               data[i];
        const uint8_t b1 = (i+1 < len) ? data[i+1] : uint8_t{0};
        const uint8_t b2 = (i+2 < len) ? data[i+2] : uint8_t{0};

        out += kAlphabet[ (b0 >> 2) & 0x3F ];
        out += kAlphabet[ ((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F) ];
        out += (i+1 < len) ? kAlphabet[ ((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03) ] : '=';
        out += (i+2 < len) ? kAlphabet[ b2 & 0x3F ] : '=';
    }
    return out;
}

/// Overload tiện lợi nhận vector<uint8_t>
[[nodiscard]] std::string base64_encode(const std::vector<uint8_t>& data)
{
    return base64_encode(data.data(), data.size());
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
// § 2.  LLMClient Static Utilities
// ══════════════════════════════════════════════════════════════════════════════

std::optional<std::string>
LLMClient::load_image_as_base64(const std::filesystem::path& path)
{
    // ── Validate extension (case-insensitive) ─────────────────────────────────
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp" && ext != ".bmp") {
        return std::nullopt; // Định dạng không được hỗ trợ
    }

    // ── Kiểm tra tồn tại ──────────────────────────────────────────────────────
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    // ── Đọc file nhị phân (binary, ate để lấy size ngay) ─────────────────────
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return std::nullopt;
    }

    const auto file_size = static_cast<std::size_t>(ifs.tellg());
    if (file_size == 0) {
        return std::nullopt;
    }

    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(file_size);

    // Dùng reinterpret_cast để đọc vào uint8_t buffer
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(file_size))) {
        return std::nullopt;
    }

    return base64_encode(buffer);
}

std::pair<std::vector<std::string>, std::vector<std::filesystem::path>>
LLMClient::load_images_as_base64(const std::vector<std::filesystem::path>& paths)
{
    std::vector<std::string>           successes;
    std::vector<std::filesystem::path> failures;

    successes.reserve(paths.size());

    for (const auto& p : paths) {
        if (auto b64 = load_image_as_base64(p)) {
            successes.push_back(std::move(*b64));
        } else {
            failures.push_back(p);
        }
    }
    return { std::move(successes), std::move(failures) };
}

// ══════════════════════════════════════════════════════════════════════════════
// § 3.  OllamaClient — Constructor
// ══════════════════════════════════════════════════════════════════════════════

OllamaClient::OllamaClient()
    : OllamaClient(LLMConfig{}) // Delegate đến constructor có config
{}

OllamaClient::OllamaClient(LLMConfig config)
    : m_config(std::move(config))
    , m_curl(curl_easy_init())       // CurlHandle = unique_ptr<CURL, CurlEasyDeleter>
{
    if (!m_curl) {
        throw std::runtime_error(
            "[OllamaClient] curl_easy_init() thất bại — libcurl không khởi tạo được. "
            "Hãy đảm bảo CurlGlobalGuard được khởi tạo trước OllamaClient.");
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// § 4.  Configuration
// ══════════════════════════════════════════════════════════════════════════════

void OllamaClient::set_config(const LLMConfig& config)
{
    m_config = config;
}

const LLMConfig& OllamaClient::get_config() const noexcept
{
    return m_config;
}

// ══════════════════════════════════════════════════════════════════════════════
// § 5.  chat() — Entry Point
// ══════════════════════════════════════════════════════════════════════════════

LLMResponse OllamaClient::chat(const std::vector<ChatMessage>& messages)
{
    if (messages.empty()) {
        return make_error(LLMErrorType::MalformedJson,
                          "messages vector is empty — không có gì để gửi.");
    }

    // ── Xây dựng payload ─────────────────────────────────────────────────────
    const nlohmann::json payload = build_payload(messages);
    const std::string    url     = m_config.base_url + "/api/chat";
    const std::string    body    = payload.dump(); // compact JSON string

    // ── Đo latency bằng steady_clock (không bị ảnh hưởng bởi NTP/DST) ────────
    const auto t_start = std::chrono::steady_clock::now();

    // ── Thực hiện HTTP POST ───────────────────────────────────────────────────
    HttpResult result = http_post(url, body);

    const auto t_end     = std::chrono::steady_clock::now();
    const long long latency_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    // ── Parse và trả về ───────────────────────────────────────────────────────
    return parse_ollama_response(result, latency_ms);
}

// ══════════════════════════════════════════════════════════════════════════════
// § 6.  build_payload() — JSON Serialization
// ══════════════════════════════════════════════════════════════════════════════

nlohmann::json OllamaClient::build_payload(
    const std::vector<ChatMessage>& messages) const
{
    nlohmann::json j;
    j["model"]  = m_config.model_name;
    j["stream"] = false; // Chế độ non-streaming → nhận một JSON object duy nhất

    // Tùy chọn inference (options block)
    nlohmann::json options;
    if (m_config.temperature >= 0.0f) {
        options["temperature"] = m_config.temperature;
    }
    if (m_config.max_tokens > 0) {
        options["num_predict"] = m_config.max_tokens;
    }
    if (!options.empty()) {
        j["options"] = std::move(options);
    }

    // Danh sách tin nhắn
    nlohmann::json jmsgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json jmsg;
        jmsg["role"]    = msg.role;
        jmsg["content"] = msg.content;

        // Chỉ thêm trường "images" khi có ảnh (multimodal)
        if (!msg.base64_images.empty()) {
            jmsg["images"] = msg.base64_images;
        }
        jmsgs.push_back(std::move(jmsg));
    }
    j["messages"] = std::move(jmsgs);

    return j;
}

// ══════════════════════════════════════════════════════════════════════════════
// § 7.  http_post() — libcurl HTTP POST với RAII hoàn toàn
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief libcurl write callback.
 * Được gọi bởi curl mỗi khi nhận được một chunk dữ liệu.
 * Tích lũy vào std::string được truyền qua userdata.
 */
size_t OllamaClient::write_callback(
    char*   ptr,
    size_t  size,
    size_t  nmemb,
    void*   userdata) noexcept
{
    const size_t total = size * nmemb;
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, total);
    return total; // Phải trả về đúng số bytes, khác → curl báo CURLE_WRITE_ERROR
}

/**
 * @brief Xây dựng CurlSlist cho HTTP headers.
 */
CurlSlist OllamaClient::build_headers() const
{
    curl_slist* raw = nullptr;
    raw = curl_slist_append(raw, "Content-Type: application/json");
    raw = curl_slist_append(raw, "Accept: application/json");

    // Wrap ngay vào RAII — nếu append fail và raw = null, unique_ptr null cũng OK
    return CurlSlist(raw);
}

OllamaClient::HttpResult
OllamaClient::http_post(const std::string& url, const std::string& json_body)
{
    HttpResult result;
    std::string response_buffer;
    response_buffer.reserve(4096); // Tránh realloc nhỏ khi Ollama trả về từng chunk

    CURL* curl = m_curl.get();

    // Reset tất cả option từ lần gọi trước (tái sử dụng handle)
    curl_easy_reset(curl);

    // ── Cấu hình curl options ─────────────────────────────────────────────────

    // URL và method
    curl_easy_setopt(curl, CURLOPT_URL,  url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // Body
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));

    // Timeout (ms)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,       m_config.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, m_config.connect_timeout_ms);

    // SSL (khi dùng Ngrok/Colab với self-signed cert)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, m_config.verify_ssl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, m_config.verify_ssl ? 2L : 0L);

    // Redirect (Ngrok hay trả về 302)
    if (m_config.follow_redirect) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
    }

    // Headers (RAII CurlSlist)
    const CurlSlist headers = build_headers();
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());

    // Write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &OllamaClient::write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response_buffer);

    // ── Thực hiện request ─────────────────────────────────────────────────────
    result.curl_code = curl_easy_perform(curl);
    result.body      = std::move(response_buffer);

    if (result.curl_code == CURLE_OK) {
        // Lấy HTTP status code
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    }
    // Nếu curl_code != CURLE_OK → status_code giữ nguyên = 0

    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
// § 8.  parse_ollama_response() — JSON Deserialization
// ══════════════════════════════════════════════════════════════════════════════

LLMResponse OllamaClient::parse_ollama_response(
    const HttpResult& result,
    long long         latency_ms) const
{
    // ── Case 1: libcurl lỗi cấp thấp (không kết nối được) ────────────────────
    if (result.curl_code != CURLE_OK) {
        const LLMErrorType err_type = map_curl_error(result.curl_code);
        const std::string  err_msg  =
            std::string("[libcurl] ") + curl_easy_strerror(result.curl_code);
        return make_error(err_type, err_msg, 0, latency_ms);
    }

    // ── Case 2: HTTP error (4xx / 5xx) ────────────────────────────────────────
    if (result.status_code != 200) {
        std::string err_detail;
        const auto err_json = nlohmann::json::parse(result.body, nullptr, false);
        if (!err_json.is_discarded() && err_json.contains("error")) {
            err_detail = err_json["error"].get<std::string>();
        } else {
            err_detail = result.body.substr(0, 512);
        }

        const std::string err_msg =
            "HTTP " + std::to_string(result.status_code) +
            " — " + err_detail;
        return make_error(LLMErrorType::HttpError, err_msg,
                          static_cast<int>(result.status_code), latency_ms);
    }

    // ── Case 3: Parse JSON response ───────────────────────────────────────────

    // Dùng allow_exceptions=false → không throw, kiểm tra is_discarded()
    const auto j = nlohmann::json::parse(result.body, nullptr, /*allow_exceptions=*/false);

    if (j.is_discarded()) {
        const std::string preview = result.body.substr(0, 256);
        return make_error(LLMErrorType::MalformedJson,
                          "Failed to parse Ollama JSON response. Preview: " + preview,
                          static_cast<int>(result.status_code), latency_ms);
    }

    // ── Bóc tách các trường cần thiết với .value() để tránh throw ────────────
    LLMResponse resp;
    resp.latency_ms   = latency_ms;
    resp.http_status  = static_cast<int>(result.status_code);
    resp.model_used   = j.value("model", m_config.model_name);

    // Token counts (Harness metrics)
    resp.prompt_tokens     = j.value("prompt_eval_count", 0);
    resp.completion_tokens = j.value("eval_count",        0);
    resp.total_tokens      = resp.prompt_tokens + resp.completion_tokens;

    // Thời gian từ Ollama (nanoseconds → milliseconds)
    const int64_t total_ns        = j.value("total_duration", int64_t{0});
    resp.ollama_total_duration_ms = total_ns / 1'000'000;

    // Kiểm tra truncation
    resp.truncated = (j.value("done_reason", std::string("stop")) == "length");

    // Nội dung chính (message.content) — dùng try-catch vì nested access
    try {
        resp.content = j.at("message").at("content").get<std::string>();
        resp.success = true;
    } catch (const nlohmann::json::exception& e) {
        // JSON hợp lệ nhưng schema không đúng — trường hợp Ollama version cũ
        return make_error(
            LLMErrorType::MalformedJson,
            std::string("Unexpected Ollama schema — ") + e.what() +
            " | Body: " + result.body.substr(0, 256),
            static_cast<int>(result.status_code), latency_ms);
    }

    return resp;
}

// ══════════════════════════════════════════════════════════════════════════════
// § 9.  Error Mapping
// ══════════════════════════════════════════════════════════════════════════════

LLMErrorType OllamaClient::map_curl_error(CURLcode code) noexcept
{
    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:   return LLMErrorType::Timeout;
        case CURLE_COULDNT_CONNECT:      return LLMErrorType::ConnectionRefused;
        case CURLE_COULDNT_RESOLVE_HOST: return LLMErrorType::ConnectionRefused;
        case CURLE_SSL_CONNECT_ERROR:    return LLMErrorType::ConnectionRefused;
        default:                         return LLMErrorType::ConnectionRefused;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// § 10. Factory Function
// ══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<LLMClient>
create_llm_client(std::string_view backend, const LLMConfig& config)
{
    if (backend == "ollama") {
        return std::make_unique<OllamaClient>(config);
    }
    // Các backend khác sẽ thêm ở đây (OpenAI, Gemini, Mock...)
    return nullptr;
}

} // namespace edu_schedule
