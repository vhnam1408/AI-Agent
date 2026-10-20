# Thử nghiệm Ollama API

## Phạm vi đối chiếu

PROJECT PLAN — EduSchedule-Agent, mục 6, tuần 1, TASK-01 yêu cầu khảo sát libcurl,
nlohmann/json, endpoint `/api/chat`, thử văn bản và kiểm tra vision; bàn giao bản thử
API cùng model/tag và lệnh chạy. Đề OOP Project 2026 AI Agent, mục III.3.1 yêu cầu
client cấu hình được URL/model/temperature/max_tokens, dùng chung giao diện text/ảnh,
xử lý timeout, connection refused và JSON phản hồi sai. Mục IV yêu cầu LLMClient
trừu tượng và OllamaClient kế thừa.

Tài liệu này giải thích bản thử hiện có, chưa phải báo cáo nghiệm thu toàn dự án.
Bộ trích xuất deadline và xác nhận ngày mơ hồ thuộc các task sau trong kế hoạch.

## Ba thư viện hoặc cơ chế cần hiểu

- libcurl phụ trách kết nối HTTP: gửi yêu cầu đến server, nhận các byte trả về.
- nlohmann/json tạo và đọc JSON. Dùng thư viện này để không phải tự nối dấu ngoặc,
  dấu nháy và escape ký tự trong chuỗi.
- Base64 đổi byte ảnh thành ký tự. Nó giúp đưa ảnh vào JSON, không phải mã hóa bảo mật.

## Một lần gọi diễn ra thế nào

1. `main()` đọc file văn bản hoặc đọc ảnh và chuyển thành Base64.
2. Dữ liệu được đặt vào `ChatMessage`.
3. `OllamaClient::chat()` gọi ba hàm: tạo payload, gửi HTTP, đọc phản hồi.
4. `main()` kiểm tra `success`; nếu thành công thì in và ghi bản thử vào file JSON.

Ví dụ request minh họa:

```json
{
  "model": "TEN_MODEL:TAG",
  "stream": false,
  "messages": [{"role": "user", "content": "What is 15 + 17?"}],
  "options": {"temperature": 0, "num_predict": 128}
}
```

Với ảnh, mỗi message có thêm `"images": ["CHUOI_BASE64"]`.
Cả hai trường hợp đều gọi `chat()` và gửi POST đến `/api/chat`.
`stream: false` nghĩa là nhận một JSON hoàn chỉnh sau khi model trả lời xong.
`max_tokens` trong cấu hình C++ được gửi sang Ollama dưới tên `num_predict`.

Ví dụ phản hồi API minh họa, không phải số liệu đo:

```json
{"model":"TEN_MODEL:TAG", "message":{"role":"assistant", "content":"32"}}
```

Client lấy chuỗi `message.content`. Chuỗi này có thể là câu trả lời thường; không bắt
buộc là một JSON thứ hai. `save_result()` tạo bản ghi JSON riêng chứa câu trả lời,
tên model, kiểu đầu vào, thời gian và trạng thái cắt ngắn.

## Đọc code khi chuẩn bị trình bày

Bắt đầu ở `src/main.cpp`. Khi gặp `client.chat(...)`, mở
`src/client/ollama_client.cpp` và đọc theo thứ tự:

- `build_payload()`: tạo model, messages, options trong JSON.
- `http_post()`: đặt URL, POST body, timeout và callback rồi gọi `curl_easy_perform()`.
- `write_callback()`: nối từng phần dữ liệu curl nhận được vào một chuỗi.
- `parse_ollama_response()`: kiểm tra lỗi mạng, HTTP, JSON rồi lấy nội dung AI.

`LLMClient` là giao diện chung. `OllamaClient` là lớp con biết cách gọi Ollama.
`unique_ptr` với hàm dọn tài nguyên giữ vai trò đóng curl handle/danh sách header
khi đối tượng hết thời gian sống. `CurlGlobalGuard` quản lý khởi tạo và dọn curl toàn cục.
Không cần tự viết thêm factory hoặc design pattern trong bản thử này.

## Lỗi và cách kiểm tra

| Trường hợp | Cách xử lý |
|---|---|
| Sai cổng hoặc Ollama không chạy | Client trả lỗi kết nối, CLI thoát với mã 1. |
| Server chậm quá timeout | Client trả Timeout. |
| HTTP 4xx/5xx | Client trả HttpError kèm mã HTTP. |
| Body không phải JSON hoặc sai kiểu message.content | Client trả MalformedJson. |
| File đầu vào không tồn tại/rỗng | CLI báo lỗi trước khi gọi model. |
| Model không hỗ trợ ảnh | Ghi nhận lỗi server; không coi là thử vision thành công. |
| Model bị giới hạn token | Lưu truncated và in cảnh báo; không đánh giá là câu trả lời đầy đủ. |

Test server giả lập có thể chứng minh request và xử lý lỗi đúng, nhưng không chứng minh
model đọc chính xác ảnh. Tên model đã cài không thay thế được kết quả chạy thực tế.

## Ghi nhận kết quả chạy thật

Lệnh đầy đủ có trong README. Với mỗi lần thử, lưu file JSON đầu ra, model/tag,
file đầu vào và nhận xét câu trả lời có đúng với đầu vào hay không.
Với ảnh thời khóa biểu, kiểm tra riêng nội dung đọc được và nội dung bị đoán sai.
Chưa ghi trạng thái hoàn thành hoặc số liệu model thật khi chưa chạy lại và đối chiếu.
