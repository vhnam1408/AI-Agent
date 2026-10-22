# Khung LLM và Agent — hướng dẫn TASK-05

## 1. Nhiệm vụ và phạm vi

TASK-05 thiết lập CMake và thiết kế giao diện `llm_client.h`, `agent_loop.h`,
`loop_detector.h`. Mục đích là để Client, Tool và Harness có thể nối với nhau.
Đầu ra này chưa đồng nghĩa hệ thống lập lịch đã hoàn chỉnh.

Đối chiếu Sheet1, dòng TASK-05:
https://docs.google.com/spreadsheets/d/1D1W44CMZDrowgiCz07pfPnjUgCwxdoTfitrn-15sIZ8/edit?gid=0#gid=0

## 2. Đọc file theo thứ tự này

| File | Công dụng |
|---|---|
| `include/client/llm_client.h` | Giao diện LLM, cấu hình, tin nhắn và kết quả. Giữ nguyên API của TASK-01. |
| `src/client/llm_client.h` | Header chuyển tiếp theo đường dẫn đầu ra trong sheet. Không định nghĩa thêm lớp. |
| `include/agent/agent_loop.h` | Khai báo quyết định, sự kiện, callback và các hàm của Agent. |
| `src/agent/agent_loop.h` | Chuyển tiếp đến header Agent chính. |
| `src/agent/agent_loop.cpp` | Phần xử lý tách từ header cũ; nối Registry và phát sự kiện sau mỗi bước. |
| `include/agent/loop_detector.h` | Các kiểu kết quả và giao diện kiểm tra lặp. Giữ thuật toán inline có sẵn. |
| `src/agent/loop_detector.h` | Chuyển tiếp đến header kiểm tra lặp chính. |
| `tests/agent_contract_test.cpp` | MockLLMClient, DummyTool và ví dụ lớp DemoAgent. Chạy hoàn toàn offline. |
| `CMakeLists.txt` | Build thư viện `agent_core`, ứng dụng và chương trình kiểm thử. |

Mỗi lớp chỉ được định nghĩa một lần trong `include/`. Các header chuyển tiếp
trong `src/` giúp đáp ứng đường dẫn trên sheet và tương thích code đang dùng.

## 3. Trước và sau thay đổi

| Trước | Sau |
|---|---|
| Header Agent chứa cả khai báo lẫn toàn bộ vòng lặp. | Header ngắn hơn; đọc cách xử lý tại `.cpp`. |
| `act()` trả chuỗi giả rằng Tool đã chạy. | Agent nhận Registry bằng `setToolRegistry()` và gọi `execute()`. |
| Chưa có Registry vẫn có thể báo đã chạy Tool. | Trả `ToolResult.success = false`, có thông báo lỗi. |
| Hook chạy trước Tool, không có kết quả Tool. | Hook chạy sau Tool, có `tool_result` và thời gian cả bước. |
| Bước lỗi LLM hoặc dừng vì lặp không được báo qua hook. | Các bước này cũng phát sự kiện với `error_message`. |
| Đến bước cuối có thể bỏ qua Tool và báo hoàn thành. | Chạy Tool ở bước cuối rồi báo đạt giới hạn nếu chưa có đáp án. |

## 4. Template Method là gì trong code này?

`run()` giữ thứ tự chung:

```text
onStart()
    -> observe(): nhận thông tin hiện tại
    -> think(): gọi LLM và nhận quyết định
    -> kiểm tra lặp
    -> nếu có đáp án: báo sự kiện rồi kết thúc
    -> act(): chạy Tool qua Registry
    -> báo sự kiện
    -> dùng kết quả Tool làm thông tin cho bước tiếp theo
onFinished()
```

`run()` không khai báo `virtual`. Các bước bên trong có `virtual`, nên lớp con
có thể thay đổi một bước mà vẫn dùng thứ tự chạy chung. Trong test, `DemoAgent`
chỉ ghi đè `parseDecision()` để đọc dữ liệu giả. Đây là ví dụ dễ trình bày.

Vòng lặp chuyển từ code cũ sang `.cpp` để dự án vẫn chạy được. Nó là cơ sở cho
TASK-25, chưa phải bằng chứng hoàn thành toàn bộ ReAct và parser JSON.

## 5. Thống nhất với Tool — TASK-06

Agent dùng đúng kiểu đã có trong `src/agent/Tool.h`:

```cpp
struct ToolResult {
    bool success;
    std::string message;
};
```

`act()` trả `ToolResult`, không còn trả `std::string`. Không tạo bản sao kiểu này.
Tool lỗi trả `success = false`; Agent chuyển thông báo lỗi sang bước tiếp theo
để LLM có cơ hội xử lý. Exception chuẩn từ Tool cũng được đổi thành kết quả lỗi.

`class ToolRegistry;` trong header là forward declaration: chỉ báo trước tên lớp.
File `.cpp` mới include đầy đủ Registry để gọi hàm của nó.

Agent mượn Registry bằng con trỏ. Registry phải còn tồn tại trong suốt `run()`.
Agent luôn gọi `registry.execute()`, không lấy Tool ra để bỏ qua kiểm tra quyền.

Lưu ý cần trao đổi với Anh: tài liệu kiến trúc nói mặc định Denied nhưng code
Registry hiện cho phép các Tool đã đăng ký khi allowlist rỗng. Lần sửa này giữ
nguyên Registry của TASK-06; demo gọi `allowTool()` tường minh và kiểm tra `denyTool()`.
Quyền chạy Tool cũng không thay thế xác nhận ghi lịch của người dùng.

## 6. Thống nhất với Harness — TASK-08

```cpp
using StepHook = std::function<void(const StepEvent&)>;
```

Harness đăng ký lambda qua `setStepHook()`. Agent không include lớp Harness.

| Thuộc tính StepEvent | Ý nghĩa |
|---|---|
| `step_index` | Số thứ tự bước, bắt đầu từ 1. |
| `thought` | Nhãn “gọi công cụ” hoặc “đưa ra đáp án”; không phải suy luận nội bộ của model. |
| `observation` | Thông tin đầu vào bước hiện tại. |
| `decision` | Quyết định của LLM; optional rỗng nếu không lấy được quyết định. |
| `tool_result` | Optional chứa kết quả khi đã gọi `act()`; rỗng ở bước trả lời hoặc dừng trước Tool. |
| `duration_ms` | Thời gian từ đầu bước đến trước callback, bao gồm Tool nếu có. |
| `is_loop_warning`, `loop_message` | Cảnh báo và mô tả lặp. |
| `error_message` | Lỗi khiến Agent dừng, như lỗi LLM hoặc lặp nghiêm trọng. |

Điểm cần báo Ân: `decision` nay là `std::optional<AgentDecision>`, phải kiểm tra
có giá trị trước khi đọc. `tool_result->success == false` là lỗi Tool có thể
phục hồi; `error_message` dùng cho lỗi khiến Agent dừng.

Callback chạy đồng bộ, không nên ném exception. Tham chiếu sự kiện chỉ sống trong
lời gọi callback; Harness muốn lưu thì copy sang cấu trúc trajectory của mình.

## 7. Giới hạn và những phần làm sau

- TASK-09: thống nhất việc dùng `std::expected` với API Client hiện có; lần này
  không đổi kiểu trả về Client vì sẽ ảnh hưởng TASK-01.
- TASK-13: parser JSON thực tế và multimodal. Parser mặc định hiện vẫn đọc
  `Action:` và dùng arguments `"{}"`; chưa tự đọc lệnh JSON từ model.
- TASK-25: hoàn thiện ReAct, lịch sử, trạng thái kết thúc và tích hợp thật.
- TASK-33: hoàn thiện thuật toán Generic Repeat/Ping-Pong và các trường hợp biên.

LoopDetector vẫn giữ thuật toán cũ; không tuyên bố đã hoàn thiện TASK-33.
Demo dùng parser giả và Tool giả, chưa đọc dữ liệu học tập thật hoặc ghi lịch.

## 8. Build và xem demo

Chạy trong thư mục dự án với compiler C++23, CMake và libcurl đã cài:

```text
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Trên máy dùng MSYS2 UCRT64, mở terminal UCRT64 để có compiler và DLL trên PATH.
Nếu dùng generator nhiều cấu hình, thêm `--config Debug` khi build và `-C Debug`
khi chạy CTest. Test HTTP dùng Python và server giả lập cục bộ, không cần Ollama.

Để xem riêng ví dụ TASK-05, chạy target `agent_contract_test` vừa build hoặc:

```text
ctest --test-dir build -R agent_contract -V
```

Kiểm thử xác nhận: kết quả Tool đi đến LLM và hook, giới hạn bước, Tool bị chặn,
Tool lỗi/exception, thiếu Registry, lỗi LLM, dừng do lặp và reset giữa hai lượt.
Các test này kiểm tra giao diện và hành vi vòng lặp, không xác nhận mô hình thật.
