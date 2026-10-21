# GHI CHÚ KỸ THUẬT — PERSISTENCE STORE, VECTOR SEARCH, ICALENDAR VÀ TRAJECTORY JSON

**Dự án:** EduSchedule-Agent  
**Phạm vi tìm hiểu:** SQLite3, SQLite Vector Search, `nomic-embed-text`, RFC 5545 (`.ics`) và Trajectory JSON  
**Trạng thái tài liệu:** Bản nghiên cứu và đề xuất thiết kế; các phương án cần nhóm xác nhận được ghi chú rõ bên dưới.  
**Ngày cập nhật:** 21/10/2026

> **Quy ước đọc tài liệu**
> - **Đã xác định từ yêu cầu hiện tại:** hệ thống cần lưu dữ liệu lâu dài, tìm kiếm ký ức theo ngữ nghĩa, xuất lịch `.ics` và ghi lại các bước chạy của agent.
> - **Đề xuất:** schema SQL, schema Trajectory JSON và các quy tắc tích hợp trong tài liệu này là đề xuất cho EduSchedule-Agent, không mặc nhiên là giao diện đã được cả nhóm thống nhất.
> - Trước khi triển khai, cần thống nhất tên trường, kiểu dữ liệu, phiên bản mô hình embedding và phương án vector search với trưởng nhóm.

---

## 1. Tóm tắt kiến trúc

EduSchedule-Agent cần lưu dữ liệu nghiệp vụ, tìm lại những thông tin liên quan, xuất lịch để người dùng nhập vào ứng dụng lịch và ghi log để đánh giá quá trình chạy. Có thể chia thành bốn phần:

| Thành phần | Vai trò | Định dạng hoặc công nghệ |
|---|---|---|
| Persistence Store | Lưu môn học, task, deadline, quan hệ phụ thuộc và ký ức | SQLite3 (`.db`) |
| Semantic Memory | Biểu diễn văn bản thành vector và tìm ký ức tương đồng | Ollama + `nomic-embed-text` + vector search |
| Calendar Export | Trao đổi các sự kiện lịch với phần mềm lịch khác | iCalendar theo RFC 5545 (`.ics`) |
| Agent Evaluation Log | Ghi đầu vào, công cụ được gọi, kết quả, lỗi và thời gian chạy | JSON theo schema do dự án quy định |

### 1.1. Luồng dữ liệu đề xuất

1. Module tiếp nhận thông tin trích xuất các môn học, công việc, hạn nộp và quan hệ phụ thuộc.
2. Module persistence kiểm tra dữ liệu rồi ghi vào SQLite. Nhiều thay đổi liên quan nên được thực hiện trong cùng một transaction.
3. Khi cần lưu hoặc tìm kiếm ký ức ngữ nghĩa, ứng dụng gửi văn bản đến Ollama để nhận embedding.
4. Vector được lưu kèm văn bản gốc và thông tin mô hình. Khi truy vấn, ứng dụng tạo embedding của câu hỏi rồi tìm các vector gần nhất.
5. Module lập lịch dùng task và dependency để tính thời gian phù hợp. Kết quả được xuất thành `.ics`.
6. Mỗi lần chạy agent có thể tạo một bản ghi Trajectory JSON để hỗ trợ debug và đánh giá.

---

# PHẦN A — PERSISTENCE STORE SQLITE3

## 2. SQLite3 là gì?

SQLite là hệ quản trị cơ sở dữ liệu quan hệ được tích hợp trực tiếp vào ứng dụng dưới dạng thư viện. Với cấu hình thông thường, ứng dụng không cần chạy một database server riêng. Cơ sở dữ liệu thường được lưu trong một tệp (ví dụ: `eduschedule.db`)

### 2.1. Vai trò trong EduSchedule-Agent

- **Lưu trữ lâu dài:** dữ liệu vẫn còn sau khi đóng và mở lại chương trình.
- **Nguồn dữ liệu dùng chung:** các module đọc/ghi cùng một nơi theo hợp đồng dữ liệu thống nhất.
- **Toàn vẹn dữ liệu:** khóa chính, khóa ngoại và `CHECK` giúp chặn một số dữ liệu không hợp lệ.
- **Giao dịch:** nhiều thay đổi có quan hệ với nhau có thể được commit cùng nhau hoặc rollback khi xảy ra lỗi.

SQLite phù hợp với ứng dụng đơn máy hoặc dữ liệu quy mô nhỏ đến vừa. Cần chú ý rằng SQLite chỉ cho phép một transaction ghi tại một thời điểm trên cùng database; thiết kế ghi đồng thời từ nhiều module nên dùng connection và transaction có kiểm soát.

## 3. Schema quan hệ đề xuất

Schema dưới đây là bản khởi đầu. Tên trường và các trạng thái cần được đồng bộ với class/enum trong mã C++ trước khi tích hợp.

```sql
-- Chạy PRAGMA này trên TỪNG connection cần dùng khóa ngoại.
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS courses (
    course_id   TEXT PRIMARY KEY,
    course_name TEXT NOT NULL,
    lecturer    TEXT
);

CREATE TABLE IF NOT EXISTS tasks (
    task_id             TEXT PRIMARY KEY,
    course_id           TEXT,
    title               TEXT NOT NULL,
    description         TEXT,
    priority            INTEGER NOT NULL DEFAULT 3
                        CHECK (priority BETWEEN 1 AND 5),
    status              TEXT NOT NULL DEFAULT 'To-do'
                        CHECK (status IN (
                            'To-do', 'In-Progress', 'Done',
                            'Cancelled', 'Blocked'
                        )),
    estimated_duration  INTEGER CHECK (
                            estimated_duration IS NULL OR estimated_duration >= 0
                        ),
    actual_duration     INTEGER CHECK (
                            actual_duration IS NULL OR actual_duration >= 0
                        ),
    -- Khuyến nghị chuẩn hóa timestamp về UTC, ví dụ 2026-10-25T01:00:00Z.
    deadline_utc        TEXT,
    created_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    FOREIGN KEY (course_id) REFERENCES courses(course_id)
        ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS task_dependencies (
    parent_task_id TEXT NOT NULL,
    child_task_id  TEXT NOT NULL,
    PRIMARY KEY (parent_task_id, child_task_id),
    CHECK (parent_task_id <> child_task_id),
    FOREIGN KEY (parent_task_id) REFERENCES tasks(task_id)
        ON DELETE CASCADE,
    FOREIGN KEY (child_task_id) REFERENCES tasks(task_id)
        ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_tasks_course_id
    ON tasks(course_id);

CREATE INDEX IF NOT EXISTS idx_tasks_deadline_utc
    ON tasks(deadline_utc);

CREATE INDEX IF NOT EXISTS idx_task_dependencies_child
    ON task_dependencies(child_task_id);
```

### 3.1. Giải thích thiết kế

- `courses`: lưu thông tin môn học.
- `tasks`: lưu công việc, trạng thái, độ ưu tiên, thời lượng ước tính/thực tế và deadline.
- `task_dependencies`: biểu diễn quan hệ “phải hoàn thành task cha trước task con”. Ví dụ, `TASK_UML` phải xong trước `TASK_CODE`.
- `ON DELETE CASCADE`: khi xóa môn học thì xóa các task thuộc môn đó; khi xóa task thì xóa các quan hệ phụ thuộc của task. Đây là quyết định nghiệp vụ cần nhóm xác nhận, vì CASCADE đồng nghĩa dữ liệu phụ thuộc cũng bị xóa.
- `CHECK (parent_task_id <> child_task_id)` ngăn task phụ thuộc trực tiếp vào chính nó. Nó **không** phát hiện được chu trình nhiều bước như A → B → C → A; cần kiểm tra chu trình bằng thuật toán ở tầng ứng dụng hoặc trigger phù hợp.
- `priority` được quy ước từ 1 đến 5 ở ví dụ này; nhóm cần thống nhất 1 là thấp nhất hay cao nhất.
- `Blocked` được dùng làm trạng thái trong schema mẫu thay cho `Block`. Nếu enum trong C++ đang dùng `Block`, hãy giữ thống nhất một cách viết xuyên suốt dự án.

### 3.2. Quy tắc lưu thời gian

Nên thống nhất timestamp trong database ở UTC, dùng một định dạng ISO 8601 có hậu tố `Z`, ví dụ `2026-10-25T01:00:00Z`. Khi hiển thị cho người dùng, chuyển sang múi giờ địa phương đã chọn. Không trộn timestamp có múi giờ khác nhau trong cùng một trường mà không có quy ước rõ ràng.

Nếu dự án cần lưu cả múi giờ ban đầu của người dùng, có thể bổ sung trường như `timezone_id` (ví dụ `Asia/Ho_Chi_Minh`) thay vì dựa hoàn toàn vào chuỗi ngày giờ.

## 4. Ví dụ ghi dữ liệu và transaction

Ví dụ nghiệp vụ: môn OOP có hai task “Thiết kế UML” và “Cài đặt mã nguồn”; UML phải hoàn thành trước khi bắt đầu code.

```sql
BEGIN;

INSERT INTO courses (course_id, course_name, lecturer)
VALUES ('OOP', 'Lập trình hướng đối tượng', NULL);

INSERT INTO tasks (
    task_id, course_id, title, priority, status, deadline_utc
) VALUES (
    'TASK_UML', 'OOP', 'Thiết kế UML', 4, 'To-do', '2026-10-30T16:59:59Z'
);

INSERT INTO tasks (
    task_id, course_id, title, priority, status, deadline_utc
) VALUES (
    'TASK_CODE', 'OOP', 'Cài đặt mã nguồn', 4, 'To-do', NULL
);

INSERT INTO task_dependencies (parent_task_id, child_task_id)
VALUES ('TASK_UML', 'TASK_CODE');

COMMIT;
```

Nếu có câu lệnh thất bại, ứng dụng cần thực hiện `ROLLBACK` thay vì để database ở trạng thái có một phần dữ liệu đã ghi. Trong C++, nên dùng prepared statement và bind parameter, không ghép trực tiếp nội dung người dùng vào chuỗi SQL.

## 5. Các quy tắc triển khai SQLite

1. Mỗi connection cần đặt `PRAGMA foreign_keys = ON`; việc khai báo trong một connection không tự động bật cho tất cả connection khác.
2. Dùng prepared statements để tránh lỗi escape và giảm nguy cơ SQL injection.
3. Gom các thao tác liên quan trong một transaction.
4. Kiểm tra kết quả trả về của từng lời gọi SQLite và log lỗi có ích; không chỉ giả định câu lệnh đã thành công.
5. Thống nhất vòng đời connection và việc đóng statement.
6. Cân nhắc chỉ mục cho trường thường dùng để lọc/join, nhưng không tạo chỉ mục cho mọi trường nếu chưa có nhu cầu.
7. Kiểm tra chính sách xóa dữ liệu, nhất là các quan hệ `ON DELETE CASCADE`.

---

# PHẦN B — VECTOR SEARCH VÀ NOMIC-EMBED-TEXT

## 6. Embedding và semantic search

Embedding là biểu diễn một đoạn văn bản thành một vector số. Các câu có ý nghĩa gần nhau có xu hướng tạo ra vector gần nhau trong không gian embedding, nhưng kết quả vẫn cần được đánh giá bằng dữ liệu và truy vấn thực tế.

**Ví dụ:** ký ức “Hạn nộp đồ án OOP là ngày 30/10” có thể liên quan đến câu hỏi “Khi nào cần nộp bài lớn môn Lập trình hướng đối tượng?”, dù hai câu không trùng khớp hoàn toàn về từ khóa.

Tìm kiếm ngữ nghĩa bổ sung cho tìm kiếm từ khóa; nó không thay thế hoàn toàn việc lọc theo các trường cấu trúc như `task_id`, `course_id`, trạng thái hoặc deadline.

## 7. Vai trò và cách gọi `nomic-embed-text`

`nomic-embed-text` là mô hình dùng để tạo embedding văn bản, không phải mô hình hội thoại tổng quát. Trong đồ án, Ollama có thể chạy mô hình cục bộ và cung cấp embedding qua HTTP API.

### 7.1. Tải mô hình và gọi API

```bash
ollama pull nomic-embed-text
```

Ví dụ dùng API `/api/embed` theo tài liệu Ollama hiện hành:

```bash
curl http://localhost:11434/api/embed \
  -H "Content-Type: application/json" \
  -d '{
    "model": "nomic-embed-text",
    "input": "Hạn nộp bài tập OOP là ngày 30/10"
  }'
```

Response có trường `embeddings`, chứa vector của từng phần tử đầu vào. Ứng dụng C++ cần kiểm tra mã lỗi HTTP, parse JSON và xác thực vector trước khi lưu.

Tài liệu ban đầu của nhóm giả định vector có **768 chiều**. Nên coi đây là giá trị cấu hình dự kiến và kiểm tra `embeddings[0].size()` trong runtime; không được âm thầm chấp nhận vector khác số chiều so với dữ liệu đã lưu. Ghi kèm tên/tag mô hình để biết vector được tạo bởi mô hình nào.

> Ghi chú tương thích: Ollama hiện có API `/api/embed` với trường `input`. Thư viện mô hình cũng minh họa API cũ `/api/embeddings` với trường `prompt`. Nhóm nên chọn một API dựa trên phiên bản Ollama được dùng trong dự án và ghi rõ trong hướng dẫn cài đặt. Không trộn hai request format trong cùng một client.

## 8. Thiết kế lưu embedding trong SQLite

SQLite không có một kiểu cột vector được hỗ trợ sẵn trong mọi bản cài đặt. Một cách lưu phổ biến là cột `BLOB` chứa dãy số nhị phân, kèm thông tin mô hình và số chiều. Để phân biệt dữ liệu nghiệp vụ với embedding, có thể dùng hai bảng:

```sql
CREATE TABLE IF NOT EXISTS memories (
    memory_id     TEXT PRIMARY KEY,
    content       TEXT NOT NULL,
    source_type   TEXT,
    source_id     TEXT,
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS memory_embeddings (
    id             INTEGER PRIMARY KEY,
    memory_id      TEXT NOT NULL UNIQUE,
    model_name     TEXT NOT NULL,
    dimensions     INTEGER NOT NULL CHECK (dimensions > 0),
    embedding      BLOB NOT NULL,
    embedded_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    FOREIGN KEY (memory_id) REFERENCES memories(memory_id)
        ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_memory_embeddings_model
    ON memory_embeddings(model_name, dimensions);
```

### 8.1. Hợp đồng lưu vector

Nếu chọn `BLOB` dạng `float32`, cần định nghĩa rõ cách serialize/deserialize. Một vector 768 phần tử float32 chiếm 768 × 4 = 3072 byte dữ liệu số, không tính overhead của SQLite. Cả lúc lưu và lúc đọc phải dùng cùng một quy ước byte order, kiểu số và số chiều.

Mỗi bản ghi embedding nên lưu tối thiểu:

- ID của ký ức được embedding biểu diễn.
- Tên hoặc tag mô hình embedding.
- Số chiều thực tế.
- Vector đã serialize.
- Thời điểm tạo embedding.

Nếu đổi mô hình hoặc cách tiền xử lý văn bản, các vector cũ có thể không còn so sánh trực tiếp được với vector mới. Khi đổi mô hình, nên tái tạo embedding hoặc tách dữ liệu theo phiên bản mô hình; không trộn vector khác không gian embedding trong một lượt tìm kiếm.

## 9. Hai phương án tìm kiếm vector

### Phương án A — Quét chính xác trong C++ (MVP đơn giản)

1. Lấy danh sách embedding có cùng model và dimension.
2. Deserialize từng vector.
3. Tính cosine similarity giữa vector truy vấn và từng vector lưu.
4. Sắp xếp theo điểm similarity giảm dần và lấy `top-k`.

Ưu điểm: ít phụ thuộc bên ngoài, dễ kiểm soát và phù hợp để làm nguyên mẫu. Nhược điểm: chi phí tăng theo số vector; cần benchmark nếu dữ liệu lớn lên.

Cosine similarity:

`cos(A, B) = (A · B) / (||A|| × ||B||)`

Giá trị công thức nằm trong khoảng `[-1, 1]`; giá trị càng gần `1` càng cùng hướng. Nếu một trong hai vector có norm bằng 0 thì không được chia; hãy coi đó là vector không hợp lệ.

Tài liệu Ollama nêu endpoint `/api/embed` trả về vector đã L2-normalize. Với các vector được bảo đảm chuẩn hóa, cosine similarity có thể tính bằng tích vô hướng `A · B`; vẫn cần kiểm tra vector truy vấn và dữ liệu lưu thực tế.

### Phương án B — Dùng SQLite extension có hỗ trợ vector

Nhóm có thể đánh giá một extension, ví dụ `sqlite-vec` hoặc `sqlite-vector` của SQLite-AI. Đây là các dự án khác nhau, API và cách cài đặt khác nhau; chỉ chọn **một** phương án nếu quyết định dùng extension. Việc có cột `BLOB` đơn thuần không tự tạo ra chức năng vector search.

Ví dụ minh họa theo API của extension `sqlite-vector` của SQLite-AI (không dùng nguyên xi cho `sqlite-vec`):

```sql
-- Chỉ thực hiện sau khi đã nạp extension tương ứng.
-- Hàm khởi tạo có thể cần gọi trên mỗi connection dùng vector search.
SELECT vector_init(
    'memory_embeddings',
    'embedding',
    'type=FLOAT32,dimension=768,distance=COSINE'
);
```

Cách cài extension phụ thuộc nền tảng, bản build SQLite và kiến trúc CPU. Trước khi chốt, cần thử tích hợp với bản SQLite và môi trường C++ thực tế, xác nhận cách truyền vector truy vấn, metric, thứ tự distance và điều kiện phân phối thư viện nhị phân. Không nên ghi extension là dependency bắt buộc nếu nhóm chưa đóng gói và kiểm thử nó.

## 10. Quy trình truy xuất ký ức đề xuất

1. Nhận câu hỏi.
2. Tạo embedding cho câu hỏi bằng đúng mô hình/tag đã dùng để lưu ký ức.
3. Kiểm tra HTTP status, cấu trúc JSON, số chiều và giá trị vector.
4. Tìm `top-k` vector gần nhất.
5. Kết hợp điểm semantic với bộ lọc cấu trúc (nếu có), ví dụ cùng `course_id` hoặc `source_type`.
6. Áp dụng ngưỡng điểm được thử nghiệm trên dữ liệu của nhóm; không chọn một ngưỡng tùy ý rồi coi nó đúng cho mọi câu hỏi.
7. Trả về nội dung gốc và metadata để agent dùng làm ngữ cảnh.

## 11. Fallback khi Ollama hoặc embedding lỗi

Fallback nên được triển khai như một đường đi riêng có ghi log, không phải bỏ qua lỗi âm thầm:

- **API thành công:** lưu và tìm kiếm vector bình thường.
- **Ollama không chạy, timeout hoặc mô hình chưa có:** ghi mã lỗi và chuyển sang tìm kiếm từ khóa (`LIKE` hoặc một cơ chế full-text search nếu nhóm đã cấu hình).
- **Vector rỗng, sai dimension hoặc parse lỗi:** không lưu embedding lỗi; giữ nội dung nghiệp vụ nếu phù hợp và đánh dấu việc tạo embedding thất bại để có thể thử lại.
- **Extension vector không nạp được:** nếu MVP có triển khai phương án quét C++ thì dùng phương án đó; nếu không, dùng tìm kiếm từ khóa. Không giả định fallback có sẵn nếu chưa viết và kiểm thử.

Tìm kiếm từ khóa không tương đương với semantic search; fallback chỉ giúp chức năng cơ bản vẫn hoạt động khi dịch vụ embedding không sẵn sàng.

---

# PHẦN C — RFC 5545 / ICALENDAR (`.ics`)

## 12. RFC 5545 là gì?

RFC 5545 định nghĩa định dạng iCalendar để biểu diễn và trao đổi dữ liệu lịch, sự kiện, việc cần làm và thông tin lịch khác. File `.ics` là văn bản theo cấu trúc và quy tắc cụ thể; chỉ đổi phần mở rộng của file `.txt` thành `.ics` không đủ để tạo một file lịch hợp lệ.

Trong EduSchedule-Agent, module lập lịch có thể xuất lịch học hoặc kế hoạch thực hiện task thành `schedule_output.ics` để người dùng nhập vào phần mềm lịch tương thích. Khả năng nhập, cập nhật và xử lý trùng phụ thuộc ứng dụng lịch cụ thể; không được đảm bảo mọi ứng dụng hành xử giống hệt nhau.

## 13. Cấu trúc cơ bản

- `BEGIN:VCALENDAR` / `END:VCALENDAR`: bắt đầu và kết thúc đối tượng lịch.
- `VERSION:2.0`: phiên bản iCalendar.
- `PRODID`: định danh sản phẩm tạo file.
- `BEGIN:VEVENT` / `END:VEVENT`: bao quanh một sự kiện.
- `UID`: mã định danh duy nhất và bền vững của sự kiện.
- `DTSTAMP`: dấu thời gian UTC liên quan đến thời điểm tạo/cập nhật đối tượng lịch.
- `DTSTART`, `DTEND`: thời gian bắt đầu và kết thúc.
- `SUMMARY`: tiêu đề ngắn.
- `DESCRIPTION`: mô tả chi tiết.

Trong một `VEVENT` thông thường thuộc lịch không chỉ định `METHOD`, `DTSTAMP` và `UID` là các trường bắt buộc; `DTSTART` cũng được yêu cầu trong trường hợp đó theo cấu trúc RFC 5545. `DTEND` là tùy chọn nếu dùng dạng sự kiện không có thời lượng kết thúc tường minh, nhưng nên có trong lịch học có thời lượng xác định.

## 14. Ví dụ `.ics` cho lịch học tại Việt Nam

Ví dụ buổi học UML từ **08:00 đến 10:00 ngày 25/10/2026 theo giờ Việt Nam (UTC+7)**. Nếu mã hóa bằng UTC thì giờ tương ứng là 01:00–03:00 UTC.

```ics
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//EduSchedule-Agent//Study Planner//VI
CALSCALE:GREGORIAN
BEGIN:VEVENT
UID:task-uml-session-20261025T080000@eduschedule.example
DTSTAMP:20261021T140000Z
DTSTART:20261025T010000Z
DTEND:20261025T030000Z
SUMMARY:Hoc OOP - Ve so do UML
DESCRIPTION:Lich hoc duoc sap xep tu dong boi EduSchedule-Agent.
END:VEVENT
END:VCALENDAR
```

Đây là ví dụ minh họa cấu trúc. Khi sinh file thật, ứng dụng phải tạo `DTSTAMP` tại thời điểm phù hợp, tạo UID ổn định cho sự kiện, escape nội dung và ghi từng content line kết thúc bằng `CRLF` (`\r\n`).

### 14.1. Vì sao ví dụ dùng UTC?

Ký tự `Z` chỉ UTC. `20261025T080000Z` có nghĩa 08:00 UTC, tức 15:00 tại Việt Nam (UTC+7) vào ngày đó. Vì vậy không được gắn `Z` vào giờ địa phương mà không chuyển đổi. Có thể sử dụng thời gian địa phương kèm `TZID` và định nghĩa `VTIMEZONE` tương ứng khi thiết kế yêu cầu cần vậy; phải tuân thủ quy tắc RFC 5545, không viết offset tùy ý như `+07:00` trực tiếp vào giá trị DATE-TIME iCalendar.

## 15. Quy tắc định dạng `.ics` cần xử lý

1. **CRLF:** các content line được phân tách bởi `CRLF`, không chỉ dựa vào xuống dòng theo hệ điều hành.
2. **Line folding:** dòng dài hơn 75 octet nên được gấp theo RFC 5545 bằng `CRLF` tiếp theo là một dấu cách hoặc tab. Khi tính octet, phải lưu ý ký tự UTF-8 có thể chiếm nhiều byte; không cắt giữa một chuỗi byte UTF-8.
3. **Escape TEXT:** trong thuộc tính văn bản như `SUMMARY`/`DESCRIPTION`, xử lý dấu gạch chéo ngược, dấu phẩy, dấu chấm phẩy và newline theo RFC. Ví dụ newline trong giá trị TEXT được mã hóa là `\n`; dấu phẩy và dấu chấm phẩy cần escape theo quy tắc.
4. **UID:** phải duy nhất và ổn định cho cùng một sự kiện. Khi xuất lại cùng một event, không tạo UID mới ngẫu nhiên nếu mục tiêu là giữ cùng định danh.
5. **DTSTAMP:** dùng thời gian UTC hợp lệ; không lấy nguyên giờ địa phương rồi thêm `Z`.
6. **UTF-8:** cần kiểm tra tên môn và mô tả tiếng Việt có dấu sau khi xuất và nhập lại.
7. **DTEND:** thời điểm kết thúc của sự kiện được dùng như mốc kết thúc không bao gồm thời điểm đó; nên thống nhất với module lập lịch khi xử lý khoảng thời gian liền nhau.
8. **Kết thúc sự kiện:** đảm bảo mỗi `VEVENT` được đóng bằng `END:VEVENT` và toàn lịch bằng `END:VCALENDAR`.

UID giúp nhận diện một sự kiện, nhưng không có nghĩa mọi thao tác nhập file vào mọi phần mềm lịch chắc chắn tự loại bỏ bản sao. Cách cập nhật/nhập trùng là hành vi của hệ thống nhận và luồng đồng bộ.

## 16. Checklist kiểm thử file lịch

- [ ] Có `BEGIN:VCALENDAR`, `VERSION:2.0`, `PRODID`, `END:VCALENDAR`.
- [ ] Mỗi `VEVENT` đóng/mở đúng cặp.
- [ ] Mỗi event có `UID`, `DTSTAMP` và các thuộc tính thời gian phù hợp.
- [ ] `DTSTART` và `DTEND` biểu diễn đúng thời gian dự kiến, kể cả chuyển đổi múi giờ.
- [ ] File thực tế dùng `CRLF`, xử lý line folding và escape theo RFC.
- [ ] Mô tả có tiếng Việt, dấu phẩy, dấu chấm phẩy và newline vẫn đọc đúng.
- [ ] File được thử nhập vào ít nhất một ứng dụng lịch tương thích mà nhóm chọn.
- [ ] Xuất lại cùng một event giữ UID ổn định khi đó là yêu cầu thiết kế.

---

# PHẦN D — TRAJECTORY JSON

## 17. Mục đích

Trajectory JSON là nhật ký có cấu trúc về một lần chạy agent: tác vụ nào được chạy, công cụ nào được gọi, tham số truyền vào, kết quả, lỗi và thời gian xử lý. Nó giúp debug, đo hiệu năng, kiểm tra quy trình và đánh giá thành công/thất bại.

**Lưu ý quan trọng:** “Trajectory JSON” không tự nó chỉ định một schema duy nhất cho mọi agent. Schema bên dưới là **đề xuất nội bộ cho EduSchedule-Agent**, trừ khi trưởng nhóm đã cung cấp một schema chuẩn khác. Nếu nhóm dùng định dạng đã có thì phải theo đúng tài liệu định dạng đó.

## 18. Schema đề xuất

### 18.1. Các trường cấp lần chạy

| Trường | Kiểu | Ý nghĩa |
|---|---|---|
| `schema_version` | string | Phiên bản schema của dự án |
| `run_id` | string | ID duy nhất cho một lần chạy |
| `task_id` | string | ID tác vụ nghiệp vụ được xử lý |
| `model` | string | Tên/tag model thực tế được cấu hình |
| `started_at` | string | Thời điểm bắt đầu, ISO 8601 UTC |
| `finished_at` | string | Thời điểm kết thúc, ISO 8601 UTC |
| `total_time_ms` | number | Thời gian tường tính cho toàn lượt chạy, đơn vị ms |
| `success` | boolean | Kết quả cuối cùng của lần chạy |
| `final_output` | object hoặc null | Kết quả cuối cùng đã chuẩn hóa |
| `steps` | array | Các bước hành động/công cụ trong lượt chạy |

### 18.2. Các trường cấp bước

| Trường | Kiểu | Ý nghĩa |
|---|---|---|
| `step_id` | integer | Thứ tự bước, bắt đầu từ 0 hoặc 1 theo quy ước thống nhất |
| `decision_summary` | string | Tóm tắt ngắn mục đích của hành động; không mặc định lưu toàn bộ suy luận nội bộ |
| `action.type` | string | Loại hành động, ví dụ `tool_call` |
| `action.tool` | string | Tên công cụ |
| `action.args` | object | Tham số theo cấu trúc JSON, không phải chuỗi tự do |
| `tool_result.status` | string | Ví dụ `success` hoặc `error` |
| `tool_result.data` | object hoặc null | Kết quả đã chuẩn hóa |
| `tool_result.error` | object hoặc null | Mã và mô tả lỗi nếu có |
| `latency_ms` | number | Thời gian từ khi dispatch hành động đến khi nhận kết quả công cụ |

`total_time_ms` nên đo từ lúc bắt đầu đến lúc kết thúc cả lượt chạy, còn `latency_ms` đo thời gian của từng hành động/công cụ theo định nghĩa đã thống nhất. Không bắt buộc tổng latency các công cụ bằng tổng thời gian toàn lượt, vì giữa các công cụ có thể còn xử lý điều phối hoặc các bước khác.

## 19. Ví dụ Trajectory JSON hợp lệ

```json
{
  "schema_version": "1.0",
  "run_id": "run-20261021-0001",
  "task_id": "task_01",
  "model": "gemma4-ollama",
  "started_at": "2026-10-21T14:00:00Z",
  "finished_at": "2026-10-21T14:00:05.200Z",
  "total_time_ms": 5200,
  "success": true,
  "final_output": {
    "status": "scheduled",
    "task_id": "task_01",
    "start_at": "2026-10-25T01:00:00Z",
    "end_at": "2026-10-25T03:00:00Z"
  },
  "steps": [
    {
      "step_id": 0,
      "decision_summary": "Lưu thông tin task và deadline vào bộ nhớ dài hạn.",
      "action": {
        "type": "tool_call",
        "tool": "memory_save",
        "args": {
          "course_id": "OOP",
          "task_id": "task_01",
          "deadline_utc": "2026-10-30T16:59:59Z"
        }
      },
      "tool_result": {
        "status": "success",
        "data": {
          "saved": true,
          "storage": "sqlite"
        },
        "error": null
      },
      "latency_ms": 450
    },
    {
      "step_id": 1,
      "decision_summary": "Gọi bộ lập lịch để tạo khung thời gian cho task.",
      "action": {
        "type": "tool_call",
        "tool": "scheduler_solver",
        "args": {
          "task_id": "task_01"
        }
      },
      "tool_result": {
        "status": "success",
        "data": {
          "start_at": "2026-10-25T01:00:00Z",
          "end_at": "2026-10-25T03:00:00Z"
        },
        "error": null
      },
      "latency_ms": 1200
    }
  ]
}
```

Tên model trong ví dụ chỉ là giá trị minh họa; phải thay bằng tên/tag thực tế của model khi chạy. Mỗi lần chạy nên tạo `run_id` riêng. Mã ví dụ trên biểu diễn một lần chạy thành công; nhóm cần kiểm thử cả trường hợp lỗi và timeout.

## 20. Quy tắc ghi log và bảo vệ dữ liệu

1. Dùng JSON parser/serializer thay vì tự nối chuỗi để tạo JSON, đặc biệt với nội dung có dấu nháy hoặc xuống dòng.
2. `action.args` phải là object hoặc kiểu dữ liệu được schema quy định; tránh chuyển mọi tham số thành chuỗi không có cấu trúc.
3. Thống nhất giá trị `status`, cách biểu diễn lỗi, đơn vị thời gian và múi giờ.
4. Không ghi API key, mật khẩu, token, hoặc dữ liệu cá nhân không cần thiết.
5. Ưu tiên ghi mục đích hành động, tên công cụ, đầu vào cần thiết, kết quả và lỗi. Không mặc định lưu toàn bộ suy luận nội bộ; trường `decision_summary` chỉ là tóm tắt phục vụ debug.
6. Khi schema thay đổi, tăng `schema_version` và giữ khả năng phân biệt log cũ với log mới.
7. Xác định chính sách lưu giữ log, vì trajectory có thể chứa nội dung do người dùng cung cấp.

## 21. Checklist kiểm thử Trajectory JSON

- [ ] File parse được bằng JSON parser chuẩn.
- [ ] Có `schema_version`, `run_id`, thời điểm bắt đầu/kết thúc và trạng thái cuối cùng.
- [ ] `step_id` có thứ tự ổn định.
- [ ] `action.args` đúng kiểu và phản ánh tham số thực tế.
- [ ] Bước lỗi có `tool_result.status = "error"` cùng thông tin lỗi có cấu trúc.
- [ ] Đơn vị `latency_ms` và `total_time_ms` luôn là millisecond.
- [ ] Timestamp dùng UTC ISO 8601 theo cùng một quy ước.
- [ ] Không có secret hoặc dữ liệu nhạy cảm không cần thiết trong log.
- [ ] Có ít nhất một test thành công, một test lỗi và một test timeout.

---

# PHẦN E — HỢP ĐỒNG TÍCH HỢP GIỮA CÁC MODULE

## 22. Các quy ước cần thống nhất trong nhóm

| Hạng mục | Quy ước đề xuất | Cần trưởng nhóm xác nhận? |
|---|---|---|
| ID task và course | ID chuỗi ổn định, không tái sử dụng tùy tiện | Có |
| Trạng thái task | Dùng một danh sách cố định giữa SQL và enum C++ | Có |
| Timestamp database/API | UTC ISO 8601; chuyển múi giờ ở tầng giao diện | Có |
| Embedding model | Ghi rõ tên/tag model và dimension | Có |
| Vector storage | `BLOB` float32 hoặc định dạng đã thống nhất | Có |
| Vector search | Quét C++ hoặc chọn một extension có kiểm thử | Có |
| `.ics` | RFC 5545; UID ổn định; thời gian được chuyển đổi đúng | Có |
| Trajectory JSON | Schema versioned của dự án hoặc schema bên ngoài được chỉ định | Có |
| Error handling | Lỗi có mã/trạng thái và có log; không giả định thành công | Có |

## 23. Checklist nghiệm thu tổng hợp

### SQLite và vector
- [ ] Database được tạo từ database rỗng bằng script khởi tạo.
- [ ] Khóa ngoại hoạt động trên từng connection.
- [ ] Lưu được course, task và task dependency.
- [ ] Giao dịch nhiều bước rollback đúng khi phát sinh lỗi.
- [ ] Embedding được lưu kèm model và dimension.
- [ ] Query embedding có cùng dimension/model với vector lưu.
- [ ] Semantic search trả về các kết quả hợp lý trên bộ câu hỏi thử nghiệm.
- [ ] Khi Ollama không chạy, fallback hoạt động và ghi nhận nguyên nhân.

### `.ics`
- [ ] File tuân theo cấu trúc RFC 5545.
- [ ] Thời gian trong file khớp lịch người dùng sau khi chuyển UTC/múi giờ.
- [ ] Nội dung UTF-8, escape và line folding được xử lý.
- [ ] File được nhập thử vào ứng dụng lịch mục tiêu.

### Trajectory JSON
- [ ] JSON parse được và tuân theo schema đã thống nhất.
- [ ] Ghi được cả trường hợp thành công, lỗi và timeout.
- [ ] Thời gian và trạng thái cuối cùng có ý nghĩa nhất quán.
- [ ] Log không chứa secret hoặc dữ liệu không cần thiết.
