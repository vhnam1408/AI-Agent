#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain_types.h"
#include "priority.h"
#include "TaskStatus.h"

namespace edu_schedule {

// Task is a value-like domain model. Ownership of Task objects is handled by
// TaskDomain through std::unique_ptr; Task itself does not own other Tasks.
struct Task {
    TaskId id;
    std::string title;
    std::string courseId;
    std::optional<TimePoint> deadline;
    int estimatedMinutes = 0; // 0 means unknown
    int remainingMinutes = 0;
    Priority priority = Priority::Mid;
    TaskStatus status = TaskStatus::Todo;
    std::vector<TaskId> dependencies;

    [[nodiscard]] bool isOverdue(TimePoint now) const noexcept;
    [[nodiscard]] bool hasValidDuration() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace edu_schedule
