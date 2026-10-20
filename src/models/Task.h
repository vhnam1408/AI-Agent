
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain_types.h"
#include "priority.h"
#include "TaskStatus.h"

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - id không rỗng và duy nhất trong tập Task.
// - deadline là một mốc, không chiếm lịch và không tự sinh StudySession.
// - estimatedMinutes == 0 nghĩa là chưa biết; phải làm rõ trước khi xếp lịch.
// - Nếu đã biết thời lượng: 0 <= remainingMinutes <= estimatedMinutes.
// - dependencies chứa TaskId, chỉ tạo khi có căn cứ; không suy đoán từ câu nói.
// - courseId có thể rỗng; nếu có phải tham chiếu Course::id hợp lệ.
struct Task {
    TaskId id;
    std::string title;
    std::string courseId;

    std::optional<TimePoint> deadline;

    int estimatedMinutes = 0;
    int remainingMinutes = 0;

    Priority priority = Priority::Mid;
    TaskStatus status = TaskStatus::Todo;

    std::vector<TaskId> dependencies;

    [[nodiscard]]
    bool isOverdue(TimePoint now) const noexcept {
        return deadline.has_value()
            && now > *deadline
            && status != TaskStatus::Done
            && status != TaskStatus::Cancelled;
    }
};

} // namespace edu_schedule
