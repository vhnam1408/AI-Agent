# Báo cáo Khảo sát Kỹ thuật (Technical Spike)
## Dự án: EduSchedule-Agent — Tầng LLMClient
**Tác giả:** Võ Hoàng Nam | **Ngày:** 15/10/2026 | **Trạng thái:** Hoàn thành

---

## 1. Cơ chế hoạt động của Ollama `/api/chat`

### 1.1 Cấu trúc JSON Request (`stream: false`)

Khi `stream: false`, Ollama trả về **một JSON object duy nhất** thay vì nhiều dòng JSON streaming. Đây là chế độ phù hợp nhất cho thiết kế blocking client.

**Cấu trúc Request tối thiểu:**
```json
{
  "model": "qwen3-vl:7b",
  "stream": false,
  "messages": [
    {
      "role": "system",
      "content": "Bạn là trợ lý học tập thông minh."
    },
    {
      "role": "user",
      "content": "Nhắc tôi nộp bài OOP vào thứ 6."
    }
  ],
  "options": {
    "temperature": 0.7,
    "num_predict": 2048
  }
}
```

**Cấu trúc Response chuẩn:**
```json
{
  "model": "qwen3-vl:7b",
  "created_at": "2026-10-15T07:00:00.000Z",
  "message": {
    "role": "assistant",
    "content": "Tôi đã ghi nhận: nộp bài tập OOP vào thứ Sáu."
  },
  "done_reason": "stop",
  "done": true,
  "total_duration":   4532198000,
  "load_duration":     102344000,
  "prompt_eval_count":        18,
  "prompt_eval_duration":  85000000,
  "eval_count":               42,
  "eval_duration":       4344854000
}
```

---

### 1.2 So sánh Text-only vs Multimodal Payload

**Case 1 — Text-only** (dùng với mọi model):
```json
{
  "model": "gemma3:12b",
  "stream": false,
  "messages": [
    {
      "role": "user",
      "content": "Trích xuất deadline từ: 'Thứ 6 nộp bài OOP, CN thi giữa kỳ MMT'"
    }
  ]
}
```

**Case 2 — Multimodal (Vision)** (chỉ dùng với model hỗ trợ: `qwen3-vl`, `gemma4`, `llava`):
```json
{
  "model": "qwen3-vl:7b",
  "stream": false,
  "messages": [
    {
      "role": "user",
      "content": "Hãy bóc tách toàn bộ thời khóa biểu trong ảnh và trả về JSON.",
      "images": [
        "<BASE64_STRING_của_file_PNG_hoặc_JPG>"
      ]
    }
  ]
}
```

> **Sự khác biệt cốt lõi:** Trường `images` là một mảng chuỗi Base64. Mỗi phần tử là nội dung nhị phân của một file ảnh được encode Base64 (KHÔNG có prefix `data:image/png;base64,` — Ollama không cần prefix đó). Model text-only sẽ trả về HTTP 400 hoặc bỏ qua trường `images`.

---

### 1.3 Các thông số quan trọng cần trích xuất

| Trường JSON | Kiểu | Ý nghĩa | Dùng cho |
|---|---|---|---|
| `message.content` | `string` | Nội dung phản hồi | Kết quả chính |
| `prompt_eval_count` | `int` | Số token trong prompt | `Harness`: tính chi phí |
| `eval_count` | `int` | Số token model sinh ra | `Harness`: tính chi phí |
| `total_duration` | `int64` (ns) | Tổng thời gian xử lý (ns) | `Trajectory`: latency |
| `prompt_eval_duration` | `int64` (ns) | Thời gian xử lý prompt | Profiling |
| `eval_duration` | `int64` (ns) | Thời gian sinh token | Throughput (tok/s) |
| `done` | `bool` | Model đã xử lý xong | Validation |
| `done_reason` | `string` | `"stop"` / `"length"` | Phát hiện truncation |

**Công thức tính toán quan trọng:**
```
total_tokens  = prompt_eval_count + eval_count
latency_ms    = total_duration / 1_000_000   (ns → ms)
throughput    = eval_count / (eval_duration / 1e9)  (tokens/giây)
```

---

## 2. Khảo sát `libcurl` trong C++ hiện đại

### 2.1 Cơ chế CURL* Handle và WriteCallback

`libcurl` hoạt động theo mô hình **callback-driven**: thay vì trả về response qua return value, nó gọi callback function mỗi khi nhận được một chunk dữ liệu từ server.

```
curl_easy_perform()
    │
    ├─► [Network I/O] nhận chunk 1 (e.g. 1024 bytes)
    │       └─► gọi WriteCallback(ptr=chunk1, size=1, nmemb=1024, userdata=&buffer)
    │
    ├─► [Network I/O] nhận chunk 2
    │       └─► gọi WriteCallback(...)
    │
    └─► [Done] trả về CURLE_OK
```

**Signature bắt buộc của WriteCallback:**
```cpp
static size_t write_callback(
    char*   ptr,      // Con trỏ đến chunk dữ liệu vừa nhận
    size_t  size,     // Luôn = 1 (kích thước một phần tử)
    size_t  nmemb,    // Số phần tử = số bytes thực sự nhận được
    void*   userdata  // Con trỏ tùy chỉnh (truyền qua CURLOPT_WRITEDATA)
) {
    auto* buffer = static_cast<std::string*>(userdata);
    buffer->append(ptr, size * nmemb);
    return size * nmemb; // PHẢI trả về đúng số bytes đã xử lý, nếu khác → curl báo lỗi
}
```

### 2.2 Cấu hình quan trọng

```cpp
// 1. Custom Headers
curl_slist* headers = nullptr;
headers = curl_slist_append(headers, "Content-Type: application/json");
headers = curl_slist_append(headers, "Accept: application/json");
curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

// 2. Timeout (dùng _MS để chính xác hơn _TIMEOUT giây)
curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,        120000L); // 120 giây tổng
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,  10000L); // 10 giây kết nối

// 3. Ollama qua Ngrok/Colab (HTTPS tự ký)
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Tắt verify SSL cert
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // Tắt verify hostname
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Theo redirect (Ngrok hay redirect)
curl_easy_setopt(curl, CURLOPT_MAXREDIRS,       5L); // Giới hạn redirect tối đa

// 4. POST body
const std::string payload = json_obj.dump();
curl_easy_setopt(curl, CURLOPT_POST, 1L);
curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    payload.c_str());
curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
```

### 2.3 Phân tích Memory Leak & RAII Wrapper

**Các điểm rò rỉ bộ nhớ với C API thuần:**

| Tài nguyên | Hàm tạo | Hàm giải phóng | Nguy cơ leak |
|---|---|---|---|
| `CURL*` handle | `curl_easy_init()` | `curl_easy_cleanup()` | Khi exception xảy ra trước cleanup |
| `curl_slist*` headers | `curl_slist_append()` | `curl_slist_free_all()` | Khi POST thất bại sớm |
| Global state | `curl_global_init()` | `curl_global_cleanup()` | Khi chương trình thoát đột ngột |

**Giải pháp RAII với `std::unique_ptr`:**
```cpp
// Custom deleters
struct CurlDeleter    { void operator()(CURL* c)       { curl_easy_cleanup(c); }     };
struct SlistDeleter   { void operator()(curl_slist* s) { curl_slist_free_all(s); }   };

// Type aliases an toàn
using CurlHandle = std::unique_ptr<CURL,       CurlDeleter>;
using CurlSlist  = std::unique_ptr<curl_slist, SlistDeleter>;

// RAII global init (dùng một lần trong main hoặc Singleton)
struct CurlGlobalGuard {
    CurlGlobalGuard()  { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalGuard() { curl_global_cleanup(); }
    // non-copyable, non-movable
    CurlGlobalGuard(const CurlGlobalGuard&)            = delete;
    CurlGlobalGuard& operator=(const CurlGlobalGuard&) = delete;
};

// Sử dụng:
CurlHandle curl_handle(curl_easy_init(), CurlDeleter{});
// → Tự động gọi curl_easy_cleanup() khi ra khỏi scope, kể cả khi có exception
```

---

## 3. Khảo sát `nlohmann/json` + C++17/20/23

### 3.1 Parse JSON An toàn (Không crash)

**Cách 1 — `allow_exceptions = false`** (khuyến nghị):
```cpp
// Parse với flag tắt exception → trả về json::value_t::discarded nếu lỗi
const auto j = nlohmann::json::parse(raw_body, nullptr, /*allow_exceptions=*/false);

if (j.is_discarded()) {
    // Malformed JSON — xử lý lỗi
    return make_error_response(LLMErrorType::MalformedJson, "Invalid JSON from Ollama");
}
```

**Cách 2 — `try-catch`** (khi cần thông điệp lỗi chi tiết):
```cpp
try {
    const auto j = nlohmann::json::parse(raw_body);
    // ... xử lý
} catch (const nlohmann::json::parse_error& e) {
    // e.id: mã lỗi, e.byte: vị trí byte lỗi, e.what(): mô tả
    log_error("JSON parse error at byte {}: {}", e.byte, e.what());
    return make_error_response(LLMErrorType::MalformedJson, e.what());
} catch (const nlohmann::json::type_error& e) {
    // Truy cập sai kiểu (vd: j["count"].get<int>() khi thực ra là string)
    return make_error_response(LLMErrorType::MalformedJson, e.what());
}
```

**Truy cập an toàn với `.value()`:**
```cpp
// Thay vì j["eval_count"].get<int>() → có thể throw nếu key không tồn tại
// Dùng .value(key, default) → không throw, trả về default nếu thiếu
const int eval_count   = j.value("eval_count",        0);
const int prompt_count = j.value("prompt_eval_count", 0);
const bool done        = j.value("done",           false);
const std::string done_reason = j.value("done_reason", "unknown");
```

### 3.2 Structured Bindings (C++17) + `std::optional`

```cpp
// Hàm trả về optional thay vì throw khi key không tồn tại
auto safe_get_string(const nlohmann::json& j, std::string_view key)
    -> std::optional<std::string>
{
    if (auto it = j.find(key); it != j.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return std::nullopt;
}

// Structured binding kết hợp với optional
if (const auto content = safe_get_string(j["message"], "content")) {
    resp.content = *content;  // Dereference optional an toàn
} else {
    resp.error_message = "Missing 'message.content' in response";
}

// Structured bindings cho pair/tuple kết quả HTTP
const auto [http_code, body] = perform_post_request(url, payload);
//           ^^^^^^^^  ^^^^
//           C++17 structured binding — gọn hơn std::tie()
```

**`std::expected` (C++23) — Result type hiện đại:**
```cpp
#include <expected>

// Thay vì throw exception hoặc dùng error code, trả về expected
std::expected<LLMResponse, LLMError>
OllamaClient::chat(const std::vector<ChatMessage>& messages)
{
    auto result = perform_http_post(build_payload(messages));
    if (!result) {
        return std::unexpected(result.error()); // Propagate error
    }
    return parse_response(*result);
}

// Caller xử lý:
auto resp = client.chat(messages);
if (resp) {
    std::cout << resp->content;
} else {
    std::cerr << "Error: " << to_string(resp.error().type);
}
```

---

## Kết luận và Quyết định Thiết kế

| Vấn đề | Quyết định | Lý do |
|---|---|---|
| Quản lý `CURL*` | `unique_ptr` + custom deleter | Zero-overhead RAII, không leak |
| Quản lý `curl_slist*` | `unique_ptr` + custom deleter | Tương tự CURL handle |
| Parse JSON lỗi | `parse(..., false)` + kiểm tra `is_discarded()` | Không throw, dễ unit test |
| Error reporting | `LLMResponse` với `optional<LLMErrorType>` | Không ép caller dùng try-catch |
| C++ feature | C++20 baseline, `std::expected` (C++23) | Cân bằng giữa hiện đại và portable |
| Multimodal | Trường `base64_images` trong `ChatMessage` | Cùng interface, không overload riêng |
