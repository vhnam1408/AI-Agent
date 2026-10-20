
#pragma once

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - Đây là trạng thái của Task, khác SessionStatus của từng StudySession.
// - Overdue được suy ra từ deadline và thời điểm hiện tại, không thay thế
//   Todo/InProgress trong enum.
enum class TaskStatus {
    Todo,
    InProgress,
    Done,
    Blocked,
    Cancelled
};

} // namespace edu_schedule
