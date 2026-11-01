# TASK-017 — DeadlineExtractor

## A. Nền source, quyền sửa và thiết kế

- Nền đã đối chiếu: `e8a002ba5987042823d6fccd598e47ce24ec7d2c` (TASK-013).
- Branch local: `task017/nam-deadline-extractor`. Không commit/push/merge.
- Đã đọc `LLMClient`, `OllamaClient`, Multimodal, Tool Parser, `Task.h`, `Course.h`,
  `domain_types.h`, `TaskRepository.h`, `assets/input.txt`, ảnh TKB trong bộ mẫu.
- `TimePoint` dùng lại từ `src/models/domain_types.h`; không định nghĩa lại alias.
- Các file model/tools/scheduler/harness chỉ đọc, không chỉnh sửa hoặc link vào module.
- `config/deadline_extraction.json` hiện rỗng; không sửa file cấu hình cũ chưa rõ quyền.
- Root `CMakeLists.txt` và toàn bộ TASK-013 giữ nguyên. Build riêng bằng
  `tests/deadline_extractor/CMakeLists.txt`; đăng ký TASK-017 vào root CTest cần phối hợp sau.

`DeadlineExtractor` mượn `LLMClient&`, không phụ thuộc implementation Ollama.
Tầng gọi dùng lại `LLMClient::load_image_as_base64()` và
`ChatMessage::user_with_images()` đã có. Không viết lại encoder/HTTP/tool parser.
Probe live dùng OllamaClient làm implementation của interface; unit dùng mock.

## B. Những chức năng đã viết source

Thư mục đặt theo chức năng thay cho mã task:

```text
include/agent/deadline_extractor.h          Giao diện module
src/agent/deadline_extractor.cpp            Triển khai module
tests/deadline_extractor/                   Build và bộ kiểm thử
tests/fixtures/deadline_extractor/          Dữ liệu mẫu và đáp án
tests/fixtures/deadline_extractor/regression/ Output cũ và ví dụ hồi quy
config/deadline_extractor/                  Schema dữ liệu
```

Mã TASK-017 vẫn được giữ trong branch, báo cáo và tên các bài kiểm thử để truy vết.

- Nhận text, ảnh Base64 hoặc kết hợp hai nguồn; giữ source id, text, ảnh trong kết quả.
- Prompt riêng cho dữ liệu học tập, JSON object `{items:[...]}`; không gọi Tool Parser.
- Kiểm đúng key/kiểu, kind, null, số item, dung lượng, UTF-8 JSON và khóa trùng.
- Giữ môn, tiêu đề, vị trí, nguyên văn date_text, evidence, model content và metadata.
- Với text: evidence phải xuất hiện nguyên văn trong nguồn; các field phải là đoạn
  nguyên văn của evidence. Nội dung không có bằng chứng được đánh dấu invalid.
- Với ảnh: transcript chưa thể được chứng minh bằng so chuỗi; đánh dấu
  `unverified_visual_evidence`/needs_review, không cấp deadline TimePoint tự động.
- Phân biệt `submission`, `exam`, `class`, `assigned`, `other`.
  Chỉ `submission` có thể có deadline; các loại khác có thể có event_time.
- Phát hiện thiếu trường, ngày/giờ không hợp lệ, ngày-thứ mâu thuẫn, nhiều ngày/giờ,
  múi giờ không hỗ trợ và cùng công việc có thông tin thời điểm mâu thuẫn.
- Phát hiện `omitted_source_time`: bằng chứng có giờ nhưng model bỏ giờ trong
  date_text. Mục đó invalid; không tự chèn giờ, sửa raw response hoặc cấp deadline.
- Không ghi vào TaskRepository, không tạo TaskId/courseId giả, không triển khai
  quy trình xác nhận TASK-021 hoặc AgentLoop TASK-025.

## C. Contract và nguyên lý hoạt động

### Cách tổ chức code để dễ đọc

- `extract()` chỉ làm phần tạo prompt, gọi Client và chuyển lỗi LLM.
- `parse_response()` thể hiện luồng chính: kiểm schema, đọc item, kiểm bằng chứng,
  chuẩn hóa ngày rồi đánh dấu các công việc có thông tin xung đột.
- `read_work_item()` đọc từng field bằng phép gán rõ ràng;
  `check_work_item_evidence()` kiểm từng field bằng tên, không dùng mảng con trỏ.
- `normalize_date_time()` chỉ điều phối năm bước:
  `read_calendar_date()`, `read_clock_time()`, `read_weekday()`, `read_timezone()`
  và `validate_calendar_and_set_time()`.
- `DateParts` và `ClockTime` có field mang tên cụ thể, thay cho tuple ba số.
- `read_clock_candidates()` dùng chung cho đọc giờ và kiểm việc bỏ sót giờ;
  không lặp lại cùng một khối regex ở hai nơi.
- `work_item_to_json()` và `local_date_time_to_json()` xuất từng field riêng,
  thay cho một biểu thức JSON nhiều tầng ngoặc.
- Code test được trình bày nhiều dòng; giữ các ca kiểm tra đã chuẩn bị.

Các thay đổi trên nhằm làm rõ luồng đọc source; không bỏ các quy tắc kiểm năm,
ngày/thứ, bằng chứng hoặc lỗi dữ liệu. Bản tổ chức lại vẫn cần build/test trên
Kaggle; chưa có kết quả thực thi để khẳng định không có hồi quy.

```cpp
DeadlineExtractor extractor(client); // client phải sống lâu hơn extractor
ExtractionRequest request;
request.source = {"syllabus-1", text, base64_images};
// Chỉ đặt nếu người gọi có năm rõ ràng, không lấy từ đồng hồ:
// request.reference_year = 2026;
auto result = extractor.extract(request);
```

Model phải trả đúng sáu trường mỗi item:

```json
{
  "items": [{
    "kind": "submission",
    "course": "OOP",
    "title": "Báo cáo",
    "date_text": "Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59.",
    "location": null,
    "evidence": "Nộp Báo cáo OOP ngày 24/10/2026 lúc 23:59."
  }]
}
```

Ví dụ đầu ra kỳ vọng (rút gọn, **không phải kết quả executable đã chạy**):

```json
{
  "kind":"submission", "course":"OOP", "title":"Báo cáo",
  "deadline_utc":"2026-10-24T16:59:00Z",
  "validity":"valid", "missing_fields":[], "issues":[]
}
```

Chuỗi date_text được chuẩn hóa bằng C++ từ bằng chứng, không tin ngày chuẩn hóa do
model tự tạo. Schema tham khảo ở `config/deadline_extractor/deadline_response.schema.json`.
C++ dùng giới hạn 8192 **bytes** cho mỗi string, nghiêm ngặt hơn giới hạn ký tự của
schema JSON tham khảo. Lỗi cấu trúc làm cả response lỗi; lỗi semantic được giữ trên item.

`deadline_extraction_to_json()` xuất toàn bộ `items`, danh sách `tasks` chỉ gồm
submission, missing_fields, issues, validity, UTC, source và raw_model_content.
`ok=true` nghĩa là parse contract thành công, **không có nghĩa mọi item hợp lệ**.
Luôn kiểm `validity`, `all_items_valid` và issues. Item invalid/needs_review vẫn
được giữ trong tasks để không mất dữ liệu, nhưng không được dùng như deadline đã duyệt.
JSON ghi image_count để không lặp Base64; ảnh gốc vẫn được sở hữu ở result.source.

## D. Chính sách ngày tháng

- Hỗ trợ D/M[/YYYY], YYYY-MM-DD, dạng `ngày D tháng M [năm YYYY]` và HH:MM[:SS]/HHhMM.
- Hỗ trợ năm 2000..2099 để giới hạn miền hiện đại và tránh tràn TimePoint.
- Thiếu năm giữ null, trừ khi bên gọi truyền reference_year rõ ràng.
- Không tự biến năm 26 thành 2026; không diễn giải tuần này/tuần sau thành ngày.
- Thiếu giờ giữ null, không đặt 00:00 hay 23:59. HH:MM được biểu diễn ở độ chính xác phút.
- Thứ Việt Nam 2..7/Chủ nhật được so với calendar nếu đã biết ngày/tháng/năm.
- Mặc định học tập `Asia/Ho_Chi_Minh` được xử lý theo offset +07:00 trong miền hỗ trợ;
  UTC và offset nguồn ±HH:MM/±HHMM (tối đa 14:00) được hỗ trợ. Không dùng timezone
  máy chủ. Chưa dùng tzdb cho các vùng có DST hoặc thay đổi offset lịch sử/tương lai.
- `UTC+7` không đủ định dạng được hỗ trợ: báo unsupported, không tự hiểu thành UTC.
- Ngày thi/ngày học/ngày giao chỉ là event_time, không phải deadline nộp bài.
- Không có thời điểm tự động cho dữ liệu thiếu, invalid hoặc ảnh chưa được kiểm chứng.

Giới hạn: cách so field nguyên văn cố ý chặt, có thể yêu cầu sửa prompt khi model
paraphrase đúng nghĩa. Không có semantic verifier tổng quát cho mọi câu tiếng Việt;
marker nộp và format ngày hiện tại có miền hỗ trợ xác định. Không tự nối năm ở tiêu
đề toàn văn vào từng item nếu bên gọi chưa truyền context. Cùng môn/tiêu đề/loại
nhưng khác thời điểm được gắn cờ bảo thủ; có thể là các lần lặp hợp lệ cần xem lại.

## E. Kết quả bản trước và trạng thái bản mới

ZIP `task017_evidence_20261031T142042Z.zip` kiểm thử source trước lần refactor:
build PASS, unit 76/76 PASS, HTTP 6/6 PASS, root CTest 7/7 PASS; text thiếu năm
và phát hiện sai thứ PASS. Đề cương đầy đủ FAIL do evidence thiếu tên môn; vision
FAIL do location chứa nhãn `Phòng:` và date_text thiếu bằng chứng thứ.
Mã phòng trong raw vision được chép đúng nhưng không được đổi điểm sau khi sửa
chuỗi hậu xử lý. Kết quả và ZIP gốc được giữ nguyên.

ZIP tiếp theo `task017_evidence_20261031T145552Z.zip` đã kiểm đúng source refactor
và tiêu chí `task017-retention-v2`: unit 82/82, HTTP 7/7, bộ chấm 8/8, CTest
TASK-017 3/3 và root CTest 7/7 PASS. Đề cương đủ năm PASS; vision đúng 9/9 thứ,
môn và phòng. Hai bài input.txt vẫn FAIL vì model trả location=null thay vì giữ
nơi nộp LMS, dù bằng chứng đã chứa thông tin này. Giờ/ngày và xử lý năm/thứ đúng.

ZIP `task017_evidence_20261031T155513Z.zip` đã kiểm đúng bản
`task017-online-location-v3`: unit 84/84, HTTP 8/8, bộ chấm 10/10, CTest TASK-017
3/3 và root CTest 7/7 PASS. Cả ba bài text PASS, vision đúng 9/9 thứ/môn/phòng;
LMS được giữ đúng. Snapshot được đối chiếu hash và raw output giữ nguyên.
Kết quả bản này đủ để đề nghị nghiệm thu trên bộ mẫu (`READY_FOR_REVIEW`).

**Bản đổi thư mục `deadline-extractor-layout-v4`, cùng tiêu chí
`task017-retention-v2`: chưa build/test lại trên Kaggle.**
Hai file triển khai/giao diện module và dữ liệu fixture giữ nguyên bytes so với
bản v3 đã chạy; chỉ thay các đường dẫn tham chiếu trong test/CMake/notebook/docs.
Các trạng thái NOT RUN trong bảng dưới áp dụng cho cấu trúc thư mục mới.

Prompt được làm rõ: location bao gồm nơi vật lý và đích nộp trực tuyến
(nền tảng/cổng học tập/email); chỉ null khi nguồn không ghi nơi/đích đến.
Không thêm LMS mặc định và không điền hộ sau khi model đã trả null.
Hai output lỗi LMS từ ZIP mới được giữ nguyên làm regression fixtures.

| Nhóm | Trạng thái hiện tại |
|---|---|
| Unit MockLLMClient và pure parser | NOT RUN — chờ Kaggle |
| HTTP integration với OllamaClient C++ và mock server | NOT RUN — chờ Kaggle |
| Kiểm thử chính bộ tiêu chí live bằng ca đúng và lỗi đã ghi nhận | NOT RUN — chờ Kaggle |
| Build C++23 module/probe | NOT RUN — chờ Kaggle |
| Text live: đề cương có đủ năm | NOT RUN — chờ Kaggle |
| Text live: assets/input.txt thiếu năm | NOT RUN — chờ Kaggle |
| Text live: input + context 2026 (phải phát hiện sai thứ) | NOT RUN — chờ Kaggle |
| Vision live: ảnh TKB không phải deadline | NOT RUN — chờ Kaggle |
| Regression root build/CTest TASK-013 và các test sẵn có | NOT RUN — chờ Kaggle |

Unit bao phủ một/nhiều hạn, nhiều môn, không hạn, dữ liệu thiếu, mơ hồ ngày/năm,
leap day, UTC rollover, mismatch weekday, lịch học, phân loại exam/assigned,
ảnh giữ nguyên, source grounding, schema/JSON/tool JSON sai, LLM lỗi/truncated,
giới hạn và fixture lấy nguyên dòng từ assets/input.txt.

`assets/input.txt` không có năm. Thứ được in tương thích năm 2025 nhưng **không
được dùng điều đó để tự chọn 2025**. Cung cấp context 2026 phải gắn cờ ngày/thứ,
không tự thay thứ hoặc đổi năm để ép hợp lệ.

### Tiêu chí bổ sung

1. **Giữ nguyên dữ liệu model:** sáu field gốc phải khớp raw_model_content;
   không bỏ tiền tố phòng hoặc sửa dấu mã phòng sau khi model trả lời.
2. **Bằng chứng đầy đủ:** course/title/date_text/location có mặt trong evidence
   của chính item; evidence text là nguyên văn nguồn, gồm prefix môn học.
3. **Không làm mất giờ:** ba dòng có giờ trong input.txt phải giữ 23:59, 07:30,
   17:00 kể cả lúc năm chưa biết hoặc ngày-thứ đang mâu thuẫn. Dòng cuối không có
   giờ phải giữ null. Không coi mất giờ do model là nguồn thực sự thiếu giờ.
4. **Không mất môn/việc/vị trí:** đủ bốn sự kiện, đúng kind/course, tên công việc
   chứa cụm tên đã định trước, vị trí giữ mã định danh nguồn; không thiếu/thừa/trùng.
   Đề cương đủ năm còn so đúng toàn bộ title/location và thời điểm UTC/local.
5. **Không suy diễn:** thiếu năm không dùng năm hệ thống; context 2026 phải báo
   weekday_mismatch, giữ ngày gốc và không cấp deadline/event_time đã duyệt.
6. **Vision đủ ngữ cảnh:** đủ 9 class, đúng thứ ISO/môn/phòng; evidence có tiêu đề
   thứ cùng ô học. Trường location chỉ chứa identifier/campus, không nhãn Phòng:.
7. **TKB không thành deadline:** tasks rỗng, không tự tạo năm/ngày/giờ/TimePoint;
   date_text=null cho ảnh chỉ có thứ. Transcript ảnh vẫn needs_review, không được
   bỏ cảnh báo unverified_visual_evidence để báo hợp lệ giả.
8. **Bộ chấm cũng được test:** nhận fixture tổng hợp đúng, từ chối lỗi thật đã gặp
   và từ chối bản sao output cố tình bị sửa phòng. Fixture synthetic chỉ là dữ liệu
   test bộ chấm, không được ghi thành kết quả chạy model.
9. **Đúng bản source:** snapshot/hash và criteria_version ghi trong report phải
   thuộc bản chạy lại này; không dùng log của bản trước để xác nhận bản refactor.

Logic chấm được tách vào `tests/deadline_extractor/live_result_checks.py`, thay cho các hàm
dài lồng trong notebook. `live_result_checks_test.py` là CTest riêng. Các ca lỗi
thực tế trong `tests/fixtures/deadline_extractor/regression/captured_*.json` giữ nguyên bytes từ ZIP;
provenance ghi hash. Các ca `synthetic_*.json` được ghi rõ là ví dụ tổng hợp.

## F. Chạy trên Kaggle

- Phiên Ollama/Qwen3.8:27B + micromamba toolchain TASK-013 còn hoạt động: thêm cell
  chứa toàn bộ `notebooks/TASK017_Continuation.py`.
- Phiên hết: import `notebooks/TASK017_DeadlineExtractor.ipynb`, bật GPU/Internet,
  chạy các cell setup rồi cell cuối.
- Source nhúng tự chứa; không cần push GitHub. Mỗi run tạo thư mục mới, kiểm hash
  và lưu source.patch, manifest, compiler/build/CTest stdout/stderr, JSON unit/HTTP,
  raw model content trong JSON result, ảnh dẫn xuất và report.
- Text live được so với đáp án đã định trước. Vision phải không tạo submission
  tasks/deadlines và giữ các buổi học như class; transcript ảnh vẫn needs_review.
- Không tự chạy test local hoặc trong Codex sandbox. Kiểm tra cú pháp Python,
  JSON/hash và diff chỉ là kiểm tra tĩnh, không phải PASS của chương trình.

## G. Phối hợp và nghiệm thu

Cần chủ model/repository duyệt adapter từ ExtractedWorkItem sang Task/Course, cách
cấp ID, kiểm nghiệp vụ và quyết định ghi kho; giai đoạn này không sửa các interface đó.
Nếu muốn root CTest đăng ký TASK-017 thì cần duyệt thay đổi CMake gốc trước.

Chỉ kết luận kỹ thuật sau khi có log Kaggle: build/module unit/HTTP integration
đều PASS; text/ảnh đạt các tiêu chí và lỗi ngày tháng được giữ đúng; root regression
không lỗi. Nếu live model chưa đạt schema/độ chính xác thì vẫn NOT READY.

**Hiện trạng TASK-017: thuật toán/prompt bản v3 đã đạt kiểm thử trên bộ mẫu;
cấu trúc thư mục mới cần xác minh lại đường dẫn build/test trên Kaggle.
Không tự xác nhận phê duyệt nghiệm thu chính thức, không commit/push.**
