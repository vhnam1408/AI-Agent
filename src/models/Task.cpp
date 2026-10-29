#include "Task.h"

namespace edu_schedule {

bool Task::isOverdue(TimePoint now) const noexcept {
    return deadline.has_value()
        && now > *deadline
        && status != TaskStatus::Done
        && status != TaskStatus::Cancelled;
}

bool Task::hasValidDuration() const noexcept {
    if (estimatedMinutes < 0 || remainingMinutes < 0) {
        return false;
    }
    if (estimatedMinutes == 0) {
        return remainingMinutes == 0;
    }
    return remainingMinutes <= estimatedMinutes;
}

bool Task::isValid() const noexcept {
    return !id.empty() && hasValidDuration();
}

} // namespace edu_schedule
