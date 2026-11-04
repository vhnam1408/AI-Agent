# TASK-021 — Xác nhận deadline thiếu, mơ hồ hoặc mâu thuẫn

Developer: Võ Hoàng Nam (`vhnam1408`). Branch bàn giao: `Võ-Hoàng-Nam`.
Branch triển khai trước bàn giao: `task021/nam-deadline-confirmation`.
Source nền: `dea9fea52f39f7763df1ee714f595b431f794adc`.

**Kaggle run `20261103-161843-20a6b35a`: Core TASK-021 PASS; Overall FAIL do vision.**
Không build C++, chạy executable, CTest, benchmark hoặc Ollama trên local/Codex.
Notebook không có output chạy sẵn. Kiểm tra tĩnh không phải bằng chứng unit test PASS.

## A. TASK-017 đã có gì, TASK-021 bổ sung gì

TASK-017 đã có `extract()`, `parse_response()`, JSON sáu field, text/image extraction,
phân loại sự kiện, giữ evidence/raw response, `missing_fields`, `issues`,
`reference_year` tường minh, kiểm lịch/thứ/giờ/UTC offset và xung đột cùng công việc.
Chỉ Submission hợp lệ mới được cấp deadline; transcript ảnh chưa xác minh bị chặn.

TASK-021 bổ sung phiên xác nhận deterministic, câu hỏi tiếng Việt, câu trả lời có cấu
trúc, kiểm source/index/revision, lịch sử trả lời và cổng `deadline()`. Không gọi LLM
thêm, không tạo Task, ghi kho hoặc gọi Scheduler. Snapshot trích xuất vẫn giữ trạng
thái ban đầu, kể cả Invalid; xác nhận thành công có trạng thái riêng `Confirmed`.

Validator ngày giờ TASK-017 được đưa ra API public nhỏ để dùng lại. Kiểm miền giờ và
UTC offset được chia sẻ; kiểm tháng/ngày trước khi chấp nhận giá trị từ UI tránh
giá trị lớn bị thu hẹp bởi kiểu `chrono::month/day`. JSON extraction không đổi.
Quy tắc nhận diện cùng công việc được đưa ra `same_event()` mà không đổi logic.

Bổ sung phát hiện xung đột thành phần đã biết khi cùng công việc còn thiếu năm,
với UTC offset giống nhau. Không tự chọn năm để so sánh. Các occurrence khác lời
trích chưa chuẩn hóa được chặn riêng bằng `unresolved_occurrences`; không chọn hộ
một occurrence kể cả khi chưa đủ dữ liệu để chứng minh chúng khác thời điểm UTC.

## B. API đã đọc và quyết định tái sử dụng

| API/contract | Sử dụng trong TASK-021 |
|---|---|
| `DeadlineExtractor::extract()` / `LLMClient::chat()` | Giữ luồng trích xuất hiện tại; mock kiểm không gọi model thêm khi xác nhận. |
| `DeadlineExtractor::parse_response()` | Replay raw JSON + nguồn/context gốc trong constructor, không tin cache item bị sửa. |
| `ExtractedWorkItem`, `ExtractedLocalDateTime`, `ExtractionIssue`, `ExtractionValidity` | Dùng nguyên kiểu TASK-017; không định nghĩa parser/domain mới. |
| `validate_calendar_and_set_time()` và helper giờ/offset | Tái sử dụng qua `validate_date_time()` để kiểm ngày, thứ, giờ và đổi UTC. |
| `TimePoint` tại `src/models/domain_types.h` | Dùng nguyên alias, không khai báo lại. |
| `ConstraintValidator::validateTimeSlot()` | Đã đọc header/cpp: kiểm `start < end`; không phù hợp với deadline đơn còn thiếu dữ liệu. |
| `validateStudySession()`, `validateSchedule()`, `validateDependencies()` | Đã đọc contract/implementation: kiểm phiên học, lịch, dependency; TASK-021 chưa tạo lịch nên không gọi. |
| `Task`, `TaskId`, `TimeSlot`, `TaskDomain` | Đã đọc contract; không cần tạo Task/TimeSlot giả hoặc cấp TaskId trước bước tích hợp. |
| `OllamaClient` và `deadline_extractor_probe` | Notebook dùng probe hiện có cho live; không thay HTTP client. |

Không thêm dependency Scheduler/model/repository và không sửa shared contracts.

## C. Kiến trúc và tác dụng hàm mới

### API công khai

| Hàm | Chức năng và giới hạn |
|---|---|
| `DeadlineExtractor::validate_date_time(when)` | Kiểm thành phần tường minh bằng validator TASK-017; trả item `Other` tạm với event_time khi đủ/hợp lệ. Đây chỉ là kiểm thời gian, không xác minh evidence hoặc phê duyệt deadline. |
| `DeadlineExtractor::same_event(a, b)` | Chia sẻ quy tắc kind/course/title giống nhau, course/title có giá trị; không suy đoán tên gần giống. |
| `DeadlineConfirmation(extraction, index)` | Replay raw/source/context thành snapshot bất biến; lỗi raw/input -> `invalid_argument`, sai index -> `out_of_range`. Giữ response metadata. |
| `request()` | Trả bản sao source ID, index, revision, status, required_fields, issues và questions. |
| `answer(answer)` | Kiểm đúng nguồn/item/revision và explicit_confirmation; patch các optional có mặt, lưu câu trả lời rồi kiểm lại. Trả `expected<request, string>`. |
| `deadline()` | Cổng TimePoint cho Submission khi `NotRequired` hoặc `Confirmed`; Pending/NotApplicable luôn null. |
| `original()` | Snapshot extraction không bị ghi đè; còn raw content, evidence, ảnh và validity ban đầu. |
| `answers()` | Lịch sử xác nhận riêng, bao gồm dữ liệu chưa đủ/sai nhưng đã được gửi tường minh. |
| `date_time()` | Thành phần candidate để hiển thị; không lộ TimePoint chưa được duyệt. |

### Helper

- `check_clock_range()` và `supported_utc_offset()`: dùng chung miền giờ/offset,
  không tạo một bộ calendar validator thứ hai.
- `refresh()`: kiểm candidate bằng validator chung, kiểm provenance/occurrence,
  giữ issue chưa giải quyết, tạo câu hỏi và cập nhật cổng deadline.
- `nonblank()`: loại metadata chỉ có khoảng trắng.
- `require_field()`: giữ thứ tự field ổn định, không hỏi trùng.
- `supplied_all()`: yêu cầu từng thành phần được gửi tường minh, không coi dữ liệu
  được trích xuất là dữ liệu người dùng vừa xác nhận.
- `question_for()`: ánh xạ field sang câu hỏi tiếng Việt deterministic.
- `retain_issue()`: giữ lý do và field chưa giải quyết.

Các hàm test mới có comment tiếng Việt, dùng fixture cố định trong source test;
mock chỉ hiện thực interface Client, không cần model hoặc server thật.

### Quy tắc nhận xác nhận

- `explicit_confirmation` phải bắt nguồn từ thao tác của người dùng ở UI/CLI.
  Không ánh xạ trực tiếp output LLM vào cờ này hoặc các cờ xác minh.
- ID là source ID + index **trong snapshot của phiên**, không phải TaskId. Tầng
  tích hợp giữ đúng instance phiên; `revision` chống câu trả lời cũ trong instance đó.
- Optional vắng mặt không ghi đè candidate. Có thể bổ sung qua nhiều lượt.
  Từ chối sai source/index/revision/thiếu explicit_confirmation mà không đổi phiên.
- Không dùng năm máy chủ. Thiếu giờ/phút vẫn null. Giây vắng mặt biểu thị độ chính
  xác đến phút như TASK-017, không phải tự bổ sung giờ 00:00 hoặc 23:59.
- Dùng UTC offset từ extraction đã kiểm; khi bổ sung timezone, UI gửi offset phút
  tường minh. Không giải tên vùng/DST hoặc dùng timezone máy chủ. `local_date`,
  `year_origin` và tên `timezone` do answer gửi không được coi là bằng chứng.
- Năm viết tắt phải được thay bằng năm đầy đủ. Ngày/thứ mâu thuẫn vẫn bị validator
  chặn cho tới khi người dùng sửa ngày hoặc thứ sao cho khớp. Không tự bỏ thứ.
- Nhiều ngày trong một quote cần xác nhận year/month/day; nhiều giờ hoặc giờ bị
  model bỏ cần hour/minute. Nhiều occurrence cần đầy đủ date/time cùng
  `resolve_conflicts=true`; có thể bổ sung qua nhiều lượt.
- Ngôn ngữ tương đối được hỏi lại bằng timestamp tuyệt đối, kể cả khi đứng cạnh
  một ngày tuyệt đối. Nhận diện từ khóa bảo thủ, không phải parser ngôn ngữ tự do.
- `evidence_verified=true` là người dùng đã đối chiếu evidence và các field với
  nguồn gốc, kể cả ảnh; gửi đủ ngày giờ **không** tự xóa cảnh báo evidence.
- `submission_verified=true` giải quyết thiếu marker nộp khi item vốn là Submission.
  Exam/Class/Assigned/Other luôn `NotApplicable`; không được chuyển loại bằng cờ này.
- Issue/field mới chưa có policy không bị bỏ qua. Cần bổ sung policy cùng chủ module.
- Xác nhận sai sau một deadline hợp lệ thu hồi TimePoint và đưa phiên về Pending.

### Ví dụ adapter UI tối thiểu

```cpp
// extraction là kết quả TASK-017; caller giữ session theo đúng snapshot/instance.
DeadlineConfirmation session(extraction, item_index);
auto prompt = session.request();
// Hiển thị prompt + session.original(), lấy năm người dùng thực sự nhập/xác nhận.
DeadlineConfirmationAnswer reply;
reply.source_id = prompt.source_id;
reply.item_index = prompt.item_index;
reply.revision = prompt.revision;
reply.explicit_confirmation = true; // Chỉ đặt sau thao tác xác nhận của người dùng.
reply.when.year = confirmed_year;
auto updated = session.answer(reply);
if (updated && session.deadline()) {
    // TASK-025 có thể đọc TimePoint sau khi kiểm status; TASK-021 không ghi Task.
}
```

## D. Changed files và ownership

Đã kiểm `git status`, history theo path, tác giả commit và PR trước khi sửa.
Các thay đổi chưa commit sẵn có: `docs/task013_latex_v1/`, `docs/task013_latex_v2/`,
`docs/task017_latex_v1/`, `docs/task017_latex_v2/`, `notebooks/`; giữ nguyên.

Ownership xác nhận bằng commit `dea9fea52f39f7763df1ee714f595b431f794adc`,
tác giả `Vo Hoang Nam <vhnam1408@gmail.com>`, cùng
[PR #14 của vhnam1408](https://github.com/vhnam1408/AI-Agent/pull/14),
head cùng SHA, đã merge. Phạm vi TASK-021 do yêu cầu người dùng cấp phép.

| File | Thay đổi | Ownership |
|---|---|---|
| `include/agent/deadline_extractor.h` | API validator và same_event dùng lại | Nam, commit/PR #14 |
| `src/agent/deadline_extractor.cpp` | Chia sẻ validator/nhận diện event, chặn range UI và conflict thiếu năm | Nam, commit/PR #14 |
| `tests/deadline_extractor/CMakeLists.txt` | Probe std::expected, thư viện/test riêng TASK-021 | Nam, commit/PR #14 |
| `tests/deadline_extractor/live_result_checks.py` | So nguồn/quote với CRLF/LF tương đương, giữ nguyên kiểm raw fields và nội dung | Nam, commit `dea9fea` |
| `tests/deadline_extractor/live_result_checks_test.py` | Regression CRLF/LF và từ chối nguồn đổi chữ/khoảng trắng | Nam, commit `dea9fea` |
| `include/agent/deadline_confirmation.h` | Interface/state TASK-021 | File mới theo phạm vi được cấp |
| `src/agent/deadline_confirmation.cpp` | Policy xác nhận và provenance | File mới theo phạm vi được cấp |
| `tests/deadline_extractor/deadline_confirmation_test.cpp` | Unit/mock/fixture riêng TASK-021 | File mới theo phạm vi được cấp |
| `tests/deadline_extractor/AI_Agent_TASK021_Kaggle.ipynb` | Clone đúng branch/SHA, toolchain, test/evidence | File mới, đặt trong thư mục test riêng của Nam |
| `tests/deadline_extractor/AI_Agent_TASK021_Standalone_Kaggle.ipynb` | Source nhúng, setup Ollama và chạy test độc lập trên Kaggle | File mới TASK-021 |
| `tests/deadline_extractor/package_task021_source.py` | Đóng gói working tree chọn lọc và manifest SHA-256 | File mới TASK-021 |
| `tests/deadline_extractor/make_task021_standalone.py` | Tạo lại notebook standalone từ snapshot hiện tại | File mới TASK-021 |
| `docs/TASK-021.md` | Thiết kế, hướng dẫn và báo cáo | File mới theo phạm vi được cấp |

Không sửa root CMake, Scheduler, models, tools, harness, AgentLoop, test C++ TASK-017
hoặc snapshot regression. Giai đoạn triển khai ban đầu chưa commit/push/merge/rebase.
Ngày 04/11/2026, người dùng yêu cầu commit và đẩy phần TASK-021 lên branch
`Võ-Hoàng-Nam`, với commit message:
`feat{task-21}: Xử lý trường thông tin thiếu, ngày tháng mơ hồ và cơ chế yêu cầu người dùng xác nhận`.
Phạm vi bàn giao gồm 13 file trong bảng trên; các tài liệu LaTeX/notebook chưa
tracked ngoài phạm vi này được giữ tại working tree để người dùng xử lý riêng.

## E. Hướng dẫn Kaggle

1. Mặc định test trước khi commit/push: chạy script đóng gói source
   `tests/deadline_extractor/package_task021_source.py` trên local. Đây chỉ là thao
   tác ZIP/hash, không build hoặc chạy test. Upload ZIP sinh trong `output/` làm
   dữ liệu đầu vào riêng tư của notebook Kaggle.
2. Import lại `tests/deadline_extractor/AI_Agent_TASK021_Kaggle.ipynb` đã cập nhật,
   bật Internet; chỉ bật GPU khi cần live. Giữ `SOURCE_MODE="upload"`,
   `SOURCE_PATH=""`, `EXPECTED_COMMIT=""`. Nếu muốn clone sau khi đã push, chọn
   `SOURCE_MODE="github"` và đặt GIT_REF/EXPECTED_COMMIT đúng branch/commit.
3. Notebook chuẩn bị micromamba `gxx_linux-64>=15`, CMake, libcurl, SQLite chỉ trên
   Kaggle; kiểm compiler/version và compile/run probe C++23 std::expected trước build.
   Có thể tắt cài đặt nếu toolchain Kaggle đã sẵn sàng. Thiếu môi trường -> BLOCKED.
4. Chế độ upload tự tìm một ZIP hoặc dataset đã giải nén có manifest, kiểm SHA-256
   từng file, chép source sang thư mục làm việc và ghi provenance của working tree.
   Không cần Git trong gói hoặc một commit đã push. Chế độ github vẫn clone branch,
   so SHA và checkout detached đúng commit. Cả hai chế độ đều ghi source manifest.
5. Chạy trong Kaggle:

   ```bash
   cmake -S tests/deadline_extractor -B build/task21
   cmake --build build/task21 --parallel 2
   ctest --test-dir build/task21 --output-on-failure --no-tests=error -T Test
   ```

   Notebook truyền thêm compiler và Release. Không gọi các lệnh trên ở local.
   Các test: `task17_unit`, `task17_http_integration`, `task17_live_criteria`,
   `task21_unit`. Bộ tiêu chí live dùng fixture là kiểm thử bộ chấm, không phải live model.
6. Root regression mặc định bật, dùng CMake gốc nguyên vẹn. Lỗi shared source được
   báo để nhóm phối hợp; không chỉnh code đồng đội nhằm làm build/test qua.
7. Live tùy chọn: dùng Ollama GPU Kaggle đã có từ workflow TASK-013,
   `http://127.0.0.1:11434`, đặt RUN_LIVE/LIVE_MODEL. Notebook kiểm GPU, version,
   capabilities, gọi probe C++ hiện có và bộ tiêu chí TASK-017. Không tự tải model.
8. Tải ZIP `/kaggle/working/task021-<UTC timestamp>-<id>.zip`, gửi report/log/JSON
   về để phân tích. Run mới tạo thư mục riêng; không ghi đè evidence run trước.

Evidence bao gồm SHA/ref/compiler, SHA-256 source, stdout/stderr, exit code,
CTest XML/log, unit/HTTP JSON, model/version/capabilities khi live, raw model content,
JSON output và lý do lỗi. ZIP chỉ chọn dữ liệu evidence, không đóng gói model weights,
token, toàn bộ environment hoặc repo. Report ghi riêng PASS/FAIL/BLOCKED/NOT RUN;
root hoặc live chưa chạy không được suy thành PASS từ unit.

## F. Kiểm tra tĩnh trong Codex

Các bước được phép: xem ownership/history/PR, review diff/API, `git diff --check`,
kiểm JSON notebook, AST cú pháp Python từng cell (không chạy cell), kiểm output cell
rỗng, kiểm path/test target, đối chiếu hash file tracked ngoài phạm vi với Git HEAD.
Patch riêng được xuất để developer review cả các file mới chưa tracked.

Kết quả kiểm tĩnh: `git diff --check` không có lỗi whitespace; 7 code cell Python
trong 11 cell notebook parse AST được, chưa thực thi và outputs đều rỗng.
Lần bàn giao đầu có 118 file tracked ngoài phạm vi sửa giữ nguyên, gồm 11 snapshot
regression lịch sử; sửa 3 file tracked và thêm 5 file TASK-021. Bản sửa sau review
bổ sung 2 file bộ kiểm tiêu chí của Nam, không sửa các snapshot lịch sử.

Không dùng compiler hoặc CTest local để kiểm source. Kiểm tĩnh không chứng minh
build/test PASS; kết quả Kaggle thật được ghi riêng ở mục G. File `TASK-021-static-review.json` và patch bàn giao
lần đầu nằm ở workspace cha của repository, chỉ phản ánh source trước lần sửa này.
Báo cáo tĩnh mới nằm ở `output/TASK-021-static-review.json`; không phải evidence Kaggle.

## G. Kết quả thực thi

Evidence: `task021-20261103-161843-20a6b35a.zip`, source_id
`70adbe6902c1cee4050bd08262e6f931bb7d9534a5e322730378c3dae577fed2`.
Các file C++/test/criteria và ảnh/ground truth được đối chiếu hash với source run.

| Nhóm | Trạng thái |
|---|---|
| Compiler/CMake build TASK-017/TASK-021 và root | PASS |
| Unit TASK-017 | PASS 84/84 |
| Unit/mock TASK-021 | PASS 39/39 |
| HTTP integration qua OllamaClient + mock server | PASS 8/8 |
| Bộ tiêu chí live bằng fixture | PASS 14/14 |
| Root regression | PASS 8/8 |
| Live text đủ dữ liệu, thiếu năm, context 2026 | PASS 3/3 |
| Live image Ollama GPU | FAIL: khớp đúng thứ/môn/phòng 6/9 buổi |

Core TASK-021 PASS trong bộ kiểm thử này. Overall vẫn FAIL vì live vision chưa đạt.
Các mục NOT RUN ở lịch sử bàn giao bên dưới mô tả những phiên trước, không thay
thế kết quả evidence run này.

TASK-021 test bao phủ: deadline hợp lệ; thiếu năm/tháng/ngày/giờ; năm viết tắt;
ngày không tồn tại/năm nhuận; thứ mâu thuẫn; thời gian tương đối; ngày/giờ/offset
mâu thuẫn; ảnh chưa xác minh; evidence không có trong nguồn; xác nhận thiếu/đủ/sai;
giá trị range âm/quá lớn; UTC/midnight tường minh; metadata thiếu; lịch sử/provenance;
source/index/revision sai; không gọi model thêm; không cấp deadline cho non-submission;
thu hồi deadline sau correction sai; replay chặn cache bị sửa. Fixtures lịch sử giữ nguyên.

## H. Phối hợp TASK-025 và các task khác

- TASK-025 cần UI hiển thị nguồn/ảnh/issue/câu hỏi và lấy xác nhận thực sự từ người
  dùng; giữ instance session theo snapshot, không tạo lại phiên rồi dùng reply cũ.
- Lưu snapshot và answers riêng khi cần persistence; TASK-021 hiện là phiên in-memory,
  không tự ghi repository hoặc cấp ID xuyên phiên.
- Khi chọn occurrence, tầng tích hợp phải hợp nhất/đánh dấu các occurrence khác
  của cùng sự kiện để không tạo trùng Task; xác nhận một phiên không tự duyệt phiên khác.
- Chủ model/domain/repository duyệt adapter sang Task, cấp TaskId và commit nghiệp vụ.
  Chỉ đưa `deadline()` đã duyệt vào bước đó; không dùng event_time từ validator thuần.
- Chủ Scheduler xử lý kiểm lịch/phiên/dependency sau khi Task đầy đủ; không dùng
  `TimeSlot` giả trong bước hỏi lại dữ liệu.
- Root integration/AgentLoop/CMake gốc do chủ module tích hợp sau review. TASK-021
  chưa tự đăng ký vào shared build hoặc thay JSON contract.
- Bổ sung policy/test nếu parser TASK-017 sinh issue mới, hoặc cần hiểu cách diễn
  đạt thời gian tương đối ngoài tập từ khóa bảo thủ hiện tại.

## I. Sửa sau review ngày 03/11/2026

ZIP `task021-evidence-20261102T173502Z.zip` chạy `main` tại `ed45fb3...`, chỉ có
3 test TASK-017; chưa build hoặc đăng ký `task21_unit`. Unit TASK-017 84/84 và HTTP
8/8 PASS trong run đó không chứng minh implementation TASK-021 hiện tại PASS.
Bộ tiêu chí FAIL 5/10 do source Git LF khác source CRLF trong fixture. Root build
FAIL trên GCC 13 vì thiếu `<print>`; root CTest và live không có kết quả thực thi.

Bản sửa giữ nguyên fixture lịch sử và bổ sung:

- Chỉ chuẩn hóa CRLF thành LF khi so source và tìm quote trong bộ kiểm tiêu chí;
  không nới so chữ/khoảng trắng, không đổi raw model content hoặc fields trích xuất.
  Thêm 4 ca: LF, CRLF, từ chối đổi nội dung, từ chối thêm khoảng trắng. Tổng bộ kiểm
  tiêu chí là 14 ca; chưa chạy trên Kaggle.
- Confirmation đối chiếu trọn các dòng chứa evidence, kiểm mọi vị trí quote lặp.
  Quote bị cắt đầu/cuối có nội dung yêu cầu xác minh evidence và timestamp đầy đủ;
  không tự ghép thời gian từ phần nguồn ngoài quote. Kiểm từ khóa tương đối trên
  context nguồn của quote, không quét các dòng không liên quan.
- Thêm 3 nhóm test C++ về quote cắt đầu/cuối, lựa chọn ngày tuyệt đối bị cắt,
  xác minh thiếu timestamp, timestamp thiếu xác minh, quote lặp và dòng không liên quan.
  Giữ snapshot extraction/raw/evidence ban đầu ngay cả khi người dùng xác nhận.
- Notebook dùng GCC >=15 trên Kaggle như trước, bổ sung compile/link/run probe
  `<print>` với `stdc++exp`. Thiếu hỗ trợ được báo BLOCKED trước build dự án.
- Clone bị chặn nếu SHA không khớp, source TASK-021 chưa tracked hoặc thiếu
  `task21_unit`. `overall_status` chỉ PASS khi toàn bộ nhóm bắt buộc, gồm root
  regression, PASS; live chỉ bắt buộc khi `RUN_LIVE=True`.

Để kiểm chứng trước khi commit/push, dùng chế độ upload trong mục E và J;
giữ `INSTALL_TOOLCHAIN=True`, `RUN_ROOT_REGRESSION=True`, rồi Run All trên Kaggle.
Nếu chỉ chạy unit/HTTP/fixture thì không cần GPU; `RUN_LIVE=False` ghi live NOT RUN.
Gửi ZIP mới có `report.json`, source manifest và `task21/task21_unit_results.json`.
Root regression lỗi ở code đồng đội phải phối hợp chủ module; không sửa để né test.

Trạng thái source sau sửa vẫn **NOT RUN — pending Kaggle validation**. Chưa chạy
C++, CTest, test Python hoặc Ollama trên local; kiểm AST/JSON/hash không phải PASS
thực thi. Hai artifact ở workspace cha của lần bàn giao đầu không bao gồm bản sửa này.

## J. Test trước khi commit/push — chế độ upload

Notebook hiện mặc định `SOURCE_MODE="upload"`. Kiểm `EXPECTED_COMMIT` chỉ nằm trong
nhánh github; không xóa kiểm source hoặc điền một SHA giả để vượt lỗi notebook cũ.

1. Dùng notebook đã cập nhật, không chạy tiếp các cell đã import từ bản github-only.
2. Upload gói `output/task021-source-<timestamp>-<id>.zip` làm dữ liệu đầu vào riêng
   tư của Kaggle. Gói gồm các file tracked hiện tại và 7 file TASK-021 mới, bao gồm
   script đóng gói. Không gồm `.git`, file build, output hay các notebook/LaTeX
   chưa tracked ngoài danh sách TASK-021.
3. Giữ `SOURCE_MODE="upload"`, `SOURCE_PATH=""`, `EXPECTED_COMMIT=""`.
   Restart Session rồi Run All. Nếu có nhiều gói, SOURCE_PATH phải chỉ đúng ZIP
   hoặc thư mục chứa `task021-source-manifest.json`; notebook không tự chọn một gói.
4. Evidence giữ manifest SHA-256 của source thực sự được chạy, `source_id`, source
   mode và base commit chỉ để tham chiếu. `commit=null` thể hiện đây là working
   tree chưa commit; không trình bày base commit như source đã kiểm thử.
5. Sau mỗi lần sửa source, chạy lại script đóng gói và upload gói mới trước khi test.
   Sau khi các test bắt buộc PASS, developer mới review và quyết định commit/push.

Gói đã giải nén bởi Kaggle cũng được nhận qua manifest. Notebook kiểm đường dẫn,
file thiếu/thừa/trùng trong ZIP và từng hash trước khi build. Các kiểm compiler,
`task21_unit`, regression và điều kiện `overall_status=PASS` vẫn giữ nguyên.

## K. Notebook tự chứa source và setup Ollama/Qwen

Dùng `tests/deadline_extractor/AI_Agent_TASK021_Standalone_Kaggle.ipynb` nếu muốn
import một file rồi chạy, không quản lý SHA GitHub hoặc source Dataset. Source ZIP
được nhúng Base64 trong notebook, kiểm SHA-256 ZIP và từng file trước khi build.
Không nhúng notebook standalone vào chính source ZIP; tránh gói tự lồng vô hạn.

Script `tests/deadline_extractor/make_task021_standalone.py` tái tạo snapshot bằng
script đóng gói, tạo các cell rồi parse AST mà không chạy project/test trên local.
Chạy lại script mỗi khi sửa source. Notebook upload/github trước đó vẫn dùng được.

Notebook standalone mặc định `RUN_LIVE=True`, `LIVE_MODEL="qwen3.8:27b"` và
`RUN_ROOT_REGRESSION=True`. Trên Kaggle, bật Internet, chọn GPU phù hợp (T4 x2 nếu
được cấp) rồi Restart Session và Run All. Muốn chỉ kiểm C++/mock trước thì đặt
`RUN_LIVE=False`; không cần GPU/model cho các nhóm đó.

Luồng chạy: nguồn nhúng → GCC >=15/C++23 probes → unit/HTTP/criteria TASK-017/021 →
root regression → GPU/Ollama/pull/warm-up Qwen → live text/image qua probe C++ →
ngrok tùy chọn → bảng trạng thái và ZIP evidence. Ngrok mặc định tắt vì probe và
Ollama chạy chung Kaggle. Nếu cần kết nối từ máy cá nhân, bật ENABLE_NGROK và cấp
Kaggle Secret `NGROK_AUTHTOKEN`; token không được đưa vào notebook hoặc evidence.

Các lỗi build/test/model được ghi riêng để cell tổng hợp vẫn xuất báo cáo. Core
PASS chỉ khi source/toolchain/build/unit/HTTP/criteria/root đều PASS; overall còn
yêu cầu GPU/Ollama/live khi bật live. HTTP 200 hoặc model warm-up không thay thế
nghiệm thu output, cũng không chứng minh luồng xác nhận UI TASK-025 đã tích hợp.

Tham khảo URL `https://www.kaggle.com/code/tdquang/qwen-3-8-27b-ngrok-ollama` do người
dùng cung cấp. Trang/API Kaggle chưa truy xuất được trong lúc soạn; không khẳng
định đã sao chép hoặc kiểm chứng từng cell ở đó. Luồng Ollama/Qwen đối chiếu tài
liệu chính thức: `https://docs.ollama.com/linux`, `https://docs.ollama.com/faq`,
`https://ollama.com/library/qwen3.8:27b`. Trạng thái vẫn NOT RUN cho đến khi có
evidence Kaggle thật của snapshot nhúng.

## L. Evidence Kaggle `20261103-154752-39d84628` và sửa môi trường

ZIP cùng tên đã được đọc: source nhúng khớp source_id `d98bf5b9...`; GPU 2 Tesla
T4, Ollama 0.40.14 và Qwen3.8:27B tải thành công. Warm-up trả `OK`; `/api/ps` ghi
model dùng 18.62 GB VRAM, context 16384. Đây là PASS cho backend/warm-up, không
phải bằng chứng extraction hoặc confirmation PASS.

Build TASK-021 và root đều exit 2. Unit TASK-017/021, HTTP và criteria NOT RUN;
live BLOCKED do chưa có probe build thành công. Overall FAIL là kết quả đúng.
Hai vấn đề môi trường thấy trực tiếp trong log:

- Micromamba bị đặt tên `task021-micromamba`: shell hook báo `Error unknown
  MAMBA_EXE ... filename must be mamba or micromamba`, nhưng lệnh probe nhỏ vẫn
  có thể exit 0, nên probe expected/print chưa đủ để chứng minh môi trường đúng.
- CMake chọn CURL hệ thống `/usr/lib/x86_64-linux-gnu/libcurl.so`, kéo theo include
  `/usr/include/x86_64-linux-gnu` trong lúc compiler dùng sysroot Conda. Chuỗi
  lỗi `__time64_t`/`time_t` cho thấy trộn header; không áp dụng gợi ý thêm `<ctime>`
  vào source đồng đội để che lỗi này.

Notebook mới dùng executable `.../task021-mamba/bin/micromamba`, thêm bin vào PATH
và đặt MAMBA_EXE đúng đường dẫn; kiểm CONDA_PREFIX qua lệnh con sau activation.
CMake/CTest gọi bằng absolute path trong toolchain, truyền cùng CMAKE_PREFIX_PATH,
CURL include/library, SQLite include/library và build RPATH. Trước khi báo toolchain
PASS, thêm CMake preflight compile/link/run `chrono` + `expected` + `print` + CURL +
SQLite; lưu dependency paths và từ chối Conda lấy dependency ngoài prefix.
Evidence build thêm CMakeCache và compile_commands để đối chiếu lần chạy tiếp.

Chỉ sửa notebook/setup và đóng gói lại; source C++ và shared CMake không được sửa
trong lần xử lý lỗi môi trường này. Bản notebook sửa vẫn NOT RUN cho đến khi chạy
lại trên Kaggle. Có thể giữ model đã tải trong phiên Kaggle; setup dùng lại model
và server nếu còn sẵn, không bắt buộc pull lại weights.

## M. Phân tích live FAIL của run `20261103-161843-20a6b35a`

Ba ca text đều PASS, gồm giữ thiếu năm unresolved và báo weekday_mismatch khi
ngữ cảnh 2026 mâu thuẫn thứ in sẵn. Cả 4 lệnh probe exit 0, HTTP 200, không truncate.
Vision FAIL ở tiêu chí `Weekday/course/room transcription differs`, không phải lỗi
token/ngrok, HTTP hoặc thiếu GPU.

Ảnh nguồn khớp SHA-256 trong evidence; đối chiếu trực quan với ground truth cho thấy:

| Buổi | Đúng trong ảnh | Raw model trả về |
|---|---|---|
| Thứ 2, Xác suất thống kê TH | `PM_B4-2_5.3 (CS2)` | `PM_B4_2_5_3 (CS2)` |
| Thứ 4, Toán tổ hợp LT | `TOÁN TỔ HỢP` | `TOÁN TÔ HỢP` |
| Thứ 4, Toán tổ hợp TH | `TOÁN TỔ HỢP` | `TOÁN TÔ HỢP` |

Model có đủ 9 buổi nhưng chỉ 6/9 tổ hợp thứ/môn/phòng đúng hoàn toàn. Raw output
và các field được giữ nguyên; không sửa punctuation hoặc tên môn để ép PASS.
Ảnh vẫn `tasks=[]`, mọi deadline/event_time đều null và 9 item đều needs_review
với unverified_visual_evidence. Cổng an toàn không suy diễn deadline vẫn hoạt động.

Phần chưa đạt nằm ở đọc chính xác ảnh của TASK-017. Cần cải thiện luồng đọc ảnh/
prompt và kiểm chứng lại bằng raw output mới trên Kaggle trước khi nâng Overall.
Không nới tiêu chí bỏ dấu tiếng Việt hoặc gộp `-`, `_`, `.` vì chúng là nội dung nguồn.
Chi tiết phân tích: `output/TASK-021-kaggle-review-20261103-161843.json`.
