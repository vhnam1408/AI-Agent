# 🎓 EduSchedule-Agent — TASK-01

> **Nhiệm vụ:** Khảo sát libcurl, nlohmann/json, Ollama `/api/chat`; thử request văn bản và kiểm tra khả năng vision  
> **Deliverable:** Bản thử nghiệm API, ghi rõ model/tag và lệnh chạy  
> **Sinh viên phụ trách:** Võ Hoàng Nam  
> **Đồ án môn:** Lập trình Hướng đối tượng (OOP) — C++17/C++20  

---

## 📌 1. Giới thiệu TASK-01

TASK-01 tập trung nghiên cứu, khảo sát và xây dựng **tầng Client giao tiếp LLM (`LLMClient`)** kết nối với dịch vụ **Ollama** chạy cục bộ thông qua giao thức HTTP REST API `/api/chat`. 

Hệ thống được thiết kế hướng đối tượng theo chuẩn nguyên tắc SOLID:
* **Single Responsibility**: `Base64` chỉ mã hóa ảnh, `LLMClient` định hình giao diện trừu tượng, `OllamaClient` phụ trách giao thức Ollama.
* **Open/Closed**: Dễ dàng thêm `OpenAIClient`, `GeminiClient` bằng cách kế thừa `LLMClient` mà không cần sửa code kiểm thử.
* **Liskov Substitution**: Bất kỳ subclass nào cũng thay thế được `LLMClient` mà không gây vỡ chương trình.
* **Dependency Inversion**: Tầng ứng dụng chỉ phụ thuộc vào abstraction `LLMClient`, không phụ thuộc trực tiếp vào triển khai `OllamaClient`.
* **RAII (Resource Acquisition Is Initialization)**: Tự động quản lý tài nguyên `CURL*` và `curl_slist*` thông qua `std::unique_ptr` kèm custom deleter, cam kết **không rò rỉ bộ nhớ (zero memory leaks)**.

---

## 📁 2. Cấu trúc thư mục TASK-01

```
AI-Agent/
├── CMakeLists.txt              # Cấu hình biên dịch CMake C++17 (link libcurl)
├── .gitignore                  # Loại trừ build/, file nhị phân *.exe, output/
├── README.md                   # Báo cáo Deliverable: Ghi rõ model/tag và lệnh chạy
│
├── docs/
│   └── technical_spike.md      # Báo cáo khảo sát kỹ thuật libcurl, nlohmann/json, Ollama
│
├── include/
│   ├── client/
│   │   ├── llm_client.h        # Interface trừu tượng LLMClient, ChatMessage, LLMResponse
│   │   └── ollama_client.h     # OllamaClient & RAII wrappers cho libcurl
│   └── nlohmann/
│       └── json.hpp            # Thư viện JSON header-only (tích hợp sẵn, chạy offline)
│
├── src/
│   ├── client/
│   │   └── ollama_client.cpp   # Cài đặt Base64 (RFC 4648), HTTP POST libcurl, parse JSON
│   └── main.cpp                # Bản thử nghiệm API: Chạy TC-01 (Text), TC-02 (Vision), TC-03 (Error)
│
└── assets/
    ├── input.txt               # Văn bản deadline mẫu phục vụ TC-01
    └── timetable.png           # Ảnh thời khóa biểu mẫu phục vụ TC-02 (Vision)
```

---

## 🤖 3. Thông tin Model / Tag đề xuất

Hệ thống hỗ trợ các model Ollama sau (cần pull trước khi chạy):

| Loại tác vụ | Model / Tag đề xuất | Dung lượng | Lệnh tải (Terminal) | Mô tả |
| :--- | :--- | :--- | :--- | :--- |
| **Multimodal Vision** *(Khuyên dùng)* | `qwen3-vl:7b` | ~5.0 GB | `ollama pull qwen3-vl:7b` | Nhận diện chữ tiếng Việt và tiếng Anh trong ảnh thời khóa biểu cực kỳ chính xác. |
| **Multimodal Vision** *(Nhẹ / Nhanh)* | `moondream` | ~1.7 GB | `ollama pull moondream` | Model gọn nhẹ, tối ưu cho máy cấu hình RAM thấp (chuyên Tiếng Anh). |
| **Text-only NLP** | `gemma3:12b` hoặc `qwen2.5:7b` | ~5-7 GB | `ollama pull gemma3:12b` | Trích xuất deadline và bóc tách thông tin từ văn bản tiếng Việt/Anh. |

> **Khởi động server Ollama:**
> ```bash
> ollama serve
> ```

---

## 🚀 4. Hướng dẫn Build & Lệnh Chạy (Commands)

### Bước 1: Biên dịch bằng CMake
```bash
# 1. Tạo thư mục build và cấu hình Makefile (MinGW hoặc MSVC)
cmake -B build -G "MinGW Makefiles"

# 2. Biên dịch target ai_agent
cmake --build build
```

Sau khi build thành công, file thực thi nằm tại: `build/ai_agent.exe`.

### Bước 2: Các lệnh chạy thử nghiệm API

#### 1. Chạy với cấu hình mặc định (localhost:11434, model `qwen3-vl:7b`):
```bash
./build/ai_agent.exe
```

#### 2. Chỉ định Server URL và Model tùy chọn:
```bash
# Thử nghiệm với model moondream (ngôn ngữ Tiếng Anh):
./build/ai_agent.exe http://localhost:11434 moondream en

# Thử nghiệm với server Ollama chạy qua Ngrok/Colab:
./build/ai_agent.exe https://xxxx.ngrok-free.app qwen3-vl:7b vi
```

#### 3. Chỉ định file văn bản hoặc file ảnh riêng từ dòng lệnh:
```bash
./build/ai_agent.exe http://localhost:11434 qwen3-vl:7b vi "assets/input.txt" "assets/timetable.png"
```

---

## 🧪 5. Kết quả các kịch bản kiểm thử (Test Cases)

* **[TC-01] Text-only Deadline Extraction**: Đọc file `assets/input.txt`, gửi request NLP tới Ollama, trích xuất danh sách deadline thành mảng JSON chuẩn gồm 3 thuộc tính: `task`, `deadline`, `subject`. Mảng JSON được in ra Terminal và tự động lưu vào `output/deadlines_output.json`.
* **[TC-02] Multimodal Vision**: Đọc file ảnh nhị phân `assets/timetable.png`, mã hóa Base64 không qua file trung gian, gửi đến model Vision, bóc tách toàn bộ lịch học thành mảng JSON chuẩn (chuẩn hóa `task`, `deadline`, `subject`). Tự động lưu vào `output/schedule_output.json`.
* **[TC-03] Fault Tolerance & Error Handling**: Thử nghiệm kết nối sai cổng (`localhost:19999`), timeout cực ngắn (1ms), và parse JSON lỗi. Hệ thống bắt lỗi mượt mà (`LLMResponse.success == false`, `error_type`), chứng minh **không có exception nào làm crash chương trình**.
