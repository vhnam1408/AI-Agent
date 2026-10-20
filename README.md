# EduSchedule-Agent

Phạm vi hiện tại là TASK-01 trong PROJECT PLAN: khảo sát libcurl, nlohmann/json,
thử Ollama `/api/chat` với văn bản và ảnh, ghi rõ model/tag cùng lệnh chạy.
Đề chính thức mục III.3.1 yêu cầu chung giao diện text/ảnh, xử lý lỗi và cấu hình client.
Bản này giữ `LLMClient` trừu tượng và `OllamaClient` kế thừa như mục IV.

TASK-01 chưa bao gồm bộ trích xuất deadline (TASK-17), xử lý ngày mơ hồ (TASK-21)
hay hệ thống Agent hoàn chỉnh. Các module đã có vẫn được giữ trong dự án.

## Chuẩn bị và build

Cần CMake 3.20+, libcurl và compiler hỗ trợ C++23 theo cấu hình chung hiện có của dự án.
Thư viện nlohmann/json đã có trong thư mục `include/`.
Ollama và model chỉ cần khi chạy demo thật. SQLite là thư viện tùy chọn cho phần sau.

Chạy các lệnh sau tại thư mục dự án. Ví dụ trên Windows với MSYS2 UCRT64:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

Nếu dùng Linux với trình biên dịch phù hợp, bỏ `-G "MinGW Makefiles"`.

## Chạy demo

Khởi động Ollama nếu server chưa chạy, sau đó xem danh sách model:

```powershell
ollama serve
# Trong terminal khác:
ollama list
```

Danh sách trên máy khi kiểm tra ngày 20/10/2026 có `qwen2.5:0.5b` và
`moondream:latest`. Có model trong danh sách chưa chứng minh model đã chạy thành công.
Thử văn bản trước, rồi thử ảnh bằng model vision:

```powershell
.\build\ai_agent.exe --model qwen2.5:0.5b --text assets/chat_prompt.txt --temperature 0 --max-tokens 128 --output output/api_text.json
.\build\ai_agent.exe --model moondream:latest --image assets/timetable.png --temperature 0 --max-tokens 256 --timeout-ms 120000 --output output/api_image.json
```

Ảnh được gửi cùng câu hỏi tiếng Anh yêu cầu mô tả nội dung nhìn thấy.
Dùng ảnh đơn giản, rõ nét trước khi đánh giá khả năng đọc thời khóa biểu tiếng Việt.
Văn bản trong `--text` được dùng nguyên làm câu hỏi; bạn có thể tự sửa file mẫu.

| Tùy chọn | Ý nghĩa | Mặc định |
|---|---|---|
| `--model` | Tên model và tag đúng như Ollama đã cài | Bắt buộc |
| `--text` / `--image` | Chọn đúng một file đầu vào | Bắt buộc |
| `--url` | Địa chỉ Ollama | `http://localhost:11434` |
| `--temperature` | Mức ngẫu nhiên, demo cho phép 0–2 | 0.7 |
| `--max-tokens` | Giới hạn token trả lời, gửi bằng `num_predict` | 2048 |
| `--timeout-ms` | Thời gian tối đa chờ request, mili giây | 120000 |
| `--insecure` | Bỏ qua kiểm tra chứng chỉ SSL (khi dùng tunnel/ngrok) | Tắt |
| `--output` | File JSON ghi nhận lần thử | `output/result.json` |

`--help` in cách chạy. Cú pháp tham số rời cũ đã được thay bằng các cờ trên.
Nếu server dùng cổng khác hoặc chạy từ xa, truyền `--url` tương ứng.

File đầu ra gồm `model_requested`, `model_used`, `input_type`, `success`, `content`,
`latency_ms`, `truncated`. Đây là bản ghi do C++ tạo bằng nlohmann/json.
`success` chỉ phản ánh gọi API và đọc phản hồi thành công, không đánh giá câu trả lời đúng.
Nội dung AI trong `content` được giữ nguyên, có thể là văn bản thường.
Nếu bị giới hạn token, chương trình lưu `truncated: true` và in cảnh báo.
Nếu HTTP hoặc JSON API lỗi, chương trình báo lỗi và không ghi đè kết quả cũ.
Không xuất thống kê token trong bản ghi này để tránh nhầm số 0 với dữ liệu API bị thiếu.

## Đọc code theo thứ tự này

1. `src/main.cpp`: xem bốn bước trong `main()` trước. Đây là luồng sử dụng chương trình.
2. `include/client/llm_client.h`: xem các struct dữ liệu và giao diện `LLMClient`.
3. `include/client/ollama_client.h`: xem lớp `OllamaClient` và các hàm nó cung cấp.
4. `src/client/ollama_client.cpp`: đọc `chat()`, sau đó lần lượt đọc
   `build_payload()`, `http_post()` và `parse_ollama_response()`.
5. Cuối cùng mới đọc hàm Base64 và các hàm hỗ trợ quản lý tài nguyên curl.

Luồng xử lý:

```text
File văn bản / ảnh
    -> ChatMessage (ảnh được mã hóa Base64)
    -> OllamaClient::chat()
    -> build_payload(): tạo JSON gửi đi
    -> http_post(): gửi POST đến /api/chat
    -> parse_ollama_response(): đọc message.content và thông tin lỗi
    -> main(): in câu trả lời, tạo bản ghi JSON và lưu file
```

Phân biệt hai việc: Ollama trả một JSON chứa `message.content`; bên trong `content`
là câu trả lời dạng chuỗi. Ở TASK-01, không bắt chuỗi đó phải là JSON hay có schema deadline.

## Những khái niệm cần hiểu

| Thành phần | Cách hiểu |
|---|---|
| `LLMConfig` | Các tùy chọn kết nối: địa chỉ, model, timeout, số token tối đa. |
| `ChatMessage` | Một tin nhắn gửi AI: vai trò, nội dung và ảnh nếu có. |
| `LLMResponse` | Kết quả gọi API: thành công hay lỗi, nội dung, token và thời gian. |
| `LLMClient` | Lớp giao diện, quy định client phải có hàm `chat()`. |
| `OllamaClient` | Lớp con thực hiện việc gọi Ollama cụ thể. |
| `virtual` / `override` | Cho phép lớp con cung cấp cách thực hiện hàm của lớp cha. |
| `optional` | Giá trị có thể không có; chẳng hạn không đọc được ảnh. |
| `unique_ptr` | Con trỏ sở hữu tài nguyên, tự giải phóng khi hết thời gian sống. |
| `CurlGlobalGuard` | Khởi tạo curl khi tạo đối tượng, dọn curl khi đối tượng bị hủy. |
| `write_callback()` | Hàm curl gọi nhiều lần để nối các phần dữ liệu nhận được. |
| Base64 | Đổi byte ảnh thành ký tự để đưa vào JSON; không phải mã hóa bảo mật. |
| `try` / `catch` | Bắt lỗi và in thông báo thay vì kết thúc chương trình đột ngột. |

Các API dùng cho Agent được giữ để những phần sau tiếp tục hoạt động.
Trong demo, `main()` tạo trực tiếp `OllamaClient`; chưa cần học factory để theo dõi luồng này.
Các trường token và thời gian dành cho phần đánh giá về sau.

## Kiểm thử

- `ollama_client_smoke_test.cpp`: kiểm tra đầu vào rỗng, mã hóa Base64, ảnh không tồn tại.
- `ollama_http_test.py`: dựng server HTTP cục bộ để kiểm tra payload văn bản/ảnh,
  lỗi HTTP, JSON sai, timeout và việc lưu kết quả của CLI. Không tải hay chạy model.

Nếu CMake tìm thấy Python 3 thì CTest có thêm test `ollama_http`.
Python chỉ dùng cho kiểm thử; ứng dụng chính vẫn viết bằng C++.
Test với server giả lập chứng minh luồng kỹ thuật, không chứng minh độ chính xác đọc ảnh
hoặc trích xuất deadline của một model thật. Khi báo cáo, ghi rõ tên model/tag, đầu vào
và kết quả của lần chạy Ollama thực tế.

`docs/technical_spike.md` giải thích HTTP/JSON/Base64 và cách nghiệm thu TASK-01.
