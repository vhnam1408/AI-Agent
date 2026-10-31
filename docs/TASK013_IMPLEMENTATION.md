# TASK-013 — Multimodal và JSON Tool Parser C++

## Trạng thái ngày 31/10/2026

Minh chứng Kaggle: `task013_parser_revision_evidence_20261031T111555Z.zip`.

| Hạng mục | Kết quả Kaggle |
|---|---|
| Build riêng TASK-013 | PASS |
| Parser C++ unit tests | 62 PASS, 0 FAIL |
| Client HTTP mock tests | 18 PASS, 0 FAIL |
| Replay native_raw.json bằng C++ | PASS |
| Native tool calling qua Client C++ | PASS |
| Multimodal ảnh gốc | 8/9; sai dấu trong một mã phòng |
| Multimodal ảnh phóng to 4 lần | 9/9 PASS |
| Multimodal ảnh gốc kèm crop chi tiết | 9/9 PASS |
| Full build theo cấu hình Kaggle | PASS |
| Root CTest | 7/7 PASS; không đăng ký memory_tool trong cấu hình này |

**Đạt kiểm thử trên ảnh mẫu, sẵn sàng đề nghị nghiệm thu (`READY_FOR_REVIEW`).**
Đây là kết quả kỹ thuật trong phạm vi mẫu, chưa phải xác nhận phê duyệt của nhóm.
Source hiện tại có bổ sung comment tiếng Việt sau lần chạy trên; đã đối chiếu tĩnh
token C++, AST Python và các lệnh CMake để xác nhận logic không đổi. Chỉnh comment
và biên dịch tài liệu LaTeX không được tính thành một lần chạy test phần mềm mới.
Build, test và inference thực tế được thực hiện trên Kaggle, không chạy local.
Nền source: `75ee6969f3eb1aeca09e8390857ea6bf9a8de32b`.

## Thay đổi và phạm vi

- `include/client/tool_parser.h`, `src/client/tool_parser.cpp`: parse native
  `message.tool_calls`, tên tool, arguments object hoặc JSON string; hỗ trợ nhiều
  calls, id/index, content JSON; từ chối JSON lỗi, thiếu trường, trùng khóa,
  arguments sai kiểu, tên không hợp lệ/không được khai báo, response chưa hoàn tất.
- `include/client/tool_chat.h`, `src/client/ollama_tool_chat.cpp` và khai báo thêm
  trong `include/client/ollama_client.h`: API `chat_with_tools()`, gửi schema,
  dùng HTTP hiện có, trả parsed calls cùng raw response và metadata.
- `tests/tool_parser_test.cpp`, `tests/tool_client_http_test.py`: bộ test độc lập.
- `tests/tool_client_probe.cpp`: parse fixture, native request C++, text và ảnh;
  bản sửa thêm ảnh chi tiết tùy chọn qua cùng `user_with_images()`.
- `assets/task013/`: fixture native gốc, provenance, đáp án chấm và prompt.
- `tests/task013/CMakeLists.txt`: build riêng hoặc được đưa vào root CTest.
- **File dùng chung `CMakeLists.txt` gốc:** chỉ bổ sung
  `src/agent/tool_registry.cpp` vào `agent_core` để giải quyết lỗi liên kết và
  đăng ký `tests/task013`. Không sửa implementation ToolRegistry.

Không thay đổi `llm_client.h`, `ollama_client.cpp`, AgentLoop, Scheduler, Tools,
Harness hoặc persistence của thành viên khác. Không triển khai TASK-017/025.
Parser chỉ biểu diễn lời gọi, không tự thực thi tool hoặc thay thế policy registry.

## Thử nghiệm lỗi mã phòng

Ảnh gốc 934×439; model trả `PM_B4_2_5_3 (CS2)` thay cho
`PM_B4-2_5.3 (CS2)`. Prompt chép từng ký tự ở bản trước vẫn chỉ đạt 8/9.
Giả thuyết: chữ nhỏ và dấu phân cách khó phân biệt; chưa chứng minh được nguyên
nhân bên trong model. Không có bằng chứng Base64 làm hỏng ảnh.

Runner chạy đúng một lần mỗi cấu hình, giữ toàn bộ raw output:

1. Ảnh gốc làm đối chứng.
2. Toàn ảnh phóng 4 lần bằng Pillow LANCZOS.
3. Ảnh gốc kèm chi tiết phóng 4 lần từ tiêu đề thứ Hai và ô thực hành thứ Hai.

Cấu hình thứ 3 là cấu hình cuối được chọn **trước khi chạy**, không lấy lần đúng
nhất để thay cho lần sai. Crop chỉ lấy pixel thật, lưu tọa độ, ảnh và SHA256.
Prompt không chứa đáp án mã phòng. Ground truth chỉ được đọc để chấm sau response.
Không sửa dấu, thay chuỗi hoặc sửa đáp án theo kết quả model. Phóng ảnh không bảo
đảm khôi phục thông tin đã mất; nếu tiếp tục sai thì cần ảnh nguồn rõ hơn.

Trong lần chạy minh chứng mới nhất, ảnh phóng to và ảnh gốc kèm crop đều khớp
đúng 9/9 bản ghi, bao gồm `PM_B4-2_5.3 (CS2)`. Ảnh gốc vẫn đạt 8/9.
Crop tọa độ cố định phục vụ ảnh mẫu; chưa phải giải pháp tự động phát hiện vùng
bảng cho mọi ảnh TKB. Kết quả một ảnh không bảo đảm độ chính xác trên ảnh khác.

## Chạy trên Kaggle

Nếu phiên Ollama/Qwen3.8:27B và toolchain cũ còn hoạt động, copy toàn bộ
`notebooks/TASK013_Parser_Continuation.py` vào một code cell mới rồi chạy.
Nếu phiên đã hết, import `notebooks/TASK013_Parser_Revision.ipynb`, bật GPU và
Internet, chạy setup rồi cell cuối. Không chạy continuation trên máy local.

Cell giải nén snapshot vào thư mục run mới, xác minh SHA256; build/test riêng;
replay fixture; gọi native tool C++ thật; chạy ba cấu hình ảnh; clean configure,
full build và full CTest. Không dùng binary cũ nếu build thất bại. Kiểm tra danh
sách root CTest thực sự có hai test TASK-013 và các test baseline.
SQLite memory test được đăng ký theo điều kiện SQLite của cấu hình gốc;
danh sách test thực chạy được lưu trong report và inventory, không ngầm coi
module tùy chọn bị bỏ qua là đã được test.

Cuối cell tải `task013_parser_revision_evidence_<timestamp>.zip`; chứa snapshot,
manifest, Git diff, stdout/stderr, raw native, raw vision, các ảnh dẫn xuất và report.

## Điều kiện đề nghị nghiệm thu

Build riêng, CTest riêng, parser unit, HTTP mock, fixture replay, native C++ thật
đều PASS; cấu hình ảnh cuối khớp nghiêm ngặt 9/9, không thiếu/thừa, không bịa giờ;
full build và full CTest PASS trên cùng snapshot Kaggle. Report chỉ chuyển
`READY_FOR_REVIEW` khi các điều kiện đó đạt. Đây là sẵn sàng để người phụ trách
duyệt nghiệm thu, không tự tuyên bố được nhóm phê duyệt.
