#include "scheduler/constraint_validator.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace edu_schedule {
namespace {

bool sameSlot(const TimeSlot& first, const TimeSlot& second) noexcept {
    return first.start == second.start && first.end == second.end;
}

bool sameFixedEvent(
    const FixedEvent& first,
    const FixedEvent& second
) noexcept {
    return first.id == second.id
        && first.title == second.title
        && sameSlot(first.time, second.time);
}

bool sameStudySession(
    const StudySession& first,
    const StudySession& second
) noexcept {
    return first.id == second.id
        && first.taskId == second.taskId
        && sameSlot(first.time, second.time)
        && first.isLocked == second.isLocked
        && first.status == second.status;
}

bool occupiesTime(SessionStatus status) noexcept {
    return status == SessionStatus::Planned
        || status == SessionStatus::Completed;
}

void append(
    ValidationResult& destination,
    ValidationResult source
) {
    destination.violations.insert(
        destination.violations.end(),
        std::make_move_iterator(source.violations.begin()),
        std::make_move_iterator(source.violations.end())
    );
}

std::chrono::minutes localMinuteOfDay(
    TimePoint point,
    std::chrono::minutes utcOffset
) {
    const TimePoint shifted = point + utcOffset;
    const auto day = std::chrono::floor<std::chrono::days>(shifted);
    return std::chrono::duration_cast<std::chrono::minutes>(shifted - day);
}

bool isInsideStudyWindow(
    const TimeSlot& slot,
    const DailyStudyWindow& window
) {
    const TimePoint shiftedStart = slot.start + window.utcOffset;
    const TimePoint shiftedEnd = slot.end + window.utcOffset;
    const auto startDay = std::chrono::floor<std::chrono::days>(shiftedStart);
    const auto endDay = std::chrono::floor<std::chrono::days>(shiftedEnd);

    if (startDay != endDay) {
        return false;
    }

    const auto startMinute = localMinuteOfDay(slot.start, window.utcOffset);
    const auto endMinute = localMinuteOfDay(slot.end, window.utcOffset);
    return startMinute >= window.start && endMinute <= window.end;
}

} // namespace

ConstraintValidator::ConstraintValidator(DailyStudyWindow studyWindow)
    : studyWindow_(studyWindow) {
    if (!studyWindow_.isValid()) {
        throw std::invalid_argument("Invalid daily study window");
    }
}

const DailyStudyWindow&
ConstraintValidator::studyWindow() const noexcept {
    return studyWindow_;
}

ValidationResult ConstraintValidator::validateTimeSlot(
    const TimeSlot& slot
) const {
    ValidationResult result;
    if (!slot.isValid()) {
        result.add({
            ConstraintViolationCode::InvalidTimeRange,
            "Time slot must satisfy start < end",
            std::nullopt,
            std::nullopt
        });
    }
    return result;
}

ValidationResult ConstraintValidator::validateStudySession(
    const StudySession& session,
    const Schedule& occupiedSchedule,
    const Task& task,
    TimePoint now
) const {
    ValidationResult result;

    if (session.id.empty() || session.taskId.empty()
        || session.taskId != task.id) {
        result.add({
            ConstraintViolationCode::InvalidTaskId,
            "Study session must have a valid ID and matching task ID",
            task.id,
            session.id
        });
    }

    append(result, validateTimeSlot(session.time));
    if (!session.time.isValid()) {
        return result;
    }

    if (task.estimatedMinutes <= 0
        || task.remainingMinutes < 0
        || task.remainingMinutes > task.estimatedMinutes) {
        result.add({
            ConstraintViolationCode::InvalidTaskDuration,
            "Task duration is unknown or inconsistent",
            task.id,
            session.id
        });
    }

    if (session.time.start < now) {
        result.add({
            ConstraintViolationCode::StartsInPast,
            "A new study session cannot start in the past",
            task.id,
            session.id
        });
    }

    if (task.deadline.has_value() && session.time.end > *task.deadline) {
        result.add({
            ConstraintViolationCode::EndsAfterDeadline,
            "Study session ends after the task deadline",
            task.id,
            session.id
        });
    }

    if (!isInsideStudyWindow(session.time, studyWindow_)) {
        result.add({
            ConstraintViolationCode::OutsideStudyWindow,
            "Study session is outside the daily study window",
            task.id,
            session.id
        });
    }

    for (const FixedEvent& event : occupiedSchedule.fixedEvents) {
        if (session.time.overlaps(event.time)) {
            result.add({
                ConstraintViolationCode::OverlapsFixedEvent,
                "Study session overlaps fixed event " + event.id,
                task.id,
                session.id
            });
        }
    }

    for (const StudySession& existing : occupiedSchedule.studySessions) {
        if (existing.id != session.id
            && occupiesTime(existing.status)
            && session.time.overlaps(existing.time)) {
            result.add({
                ConstraintViolationCode::OverlapsStudySession,
                "Study session overlaps session " + existing.id,
                task.id,
                session.id
            });
        }
    }

    return result;
}

ValidationResult ConstraintValidator::validateSchedule(
    const Schedule& proposedSchedule,
    const Schedule& currentSchedule,
    std::span<const Task> tasks,
    TimePoint now
) const {
    ValidationResult result;
    append(result, validateDependencies(tasks));
    append(
        result,
        validateProtectedEntriesUnchanged(currentSchedule, proposedSchedule)
    );

    std::unordered_set<std::string> fixedIds;
    for (const FixedEvent& event : proposedSchedule.fixedEvents) {
        if (event.id.empty() || !fixedIds.insert(event.id).second) {
            result.add({
                ConstraintViolationCode::DuplicateId,
                "Fixed event ID is empty or duplicated: " + event.id,
                std::nullopt,
                event.id
            });
        }
        append(result, validateTimeSlot(event.time));
    }

    std::unordered_set<std::string> sessionIds;
    for (const StudySession& session : proposedSchedule.studySessions) {
        if (session.id.empty() || !sessionIds.insert(session.id).second) {
            result.add({
                ConstraintViolationCode::DuplicateId,
                "Study session ID is empty or duplicated: " + session.id,
                session.taskId,
                session.id
            });
        }

        append(result, validateTimeSlot(session.time));
        const Task* task = findTask(tasks, session.taskId);
        if (task == nullptr) {
            result.add({
                ConstraintViolationCode::MissingTask,
                "Study session references missing task " + session.taskId,
                session.taskId,
                session.id
            });
            continue;
        }

        const auto currentIt = std::find_if(
            currentSchedule.studySessions.begin(),
            currentSchedule.studySessions.end(),
            [&](const StudySession& current) {
                return current.id == session.id;
            }
        );

        if (currentIt == currentSchedule.studySessions.end()) {
            append(
                result,
                validateStudySession(
                    session,
                    proposedSchedule,
                    *task,
                    now
                )
            );
        }
    }

    for (std::size_t i = 0; i < proposedSchedule.studySessions.size(); ++i) {
        const StudySession& first = proposedSchedule.studySessions[i];
        if (!occupiesTime(first.status)) {
            continue;
        }

        for (std::size_t j = i + 1;
             j < proposedSchedule.studySessions.size();
             ++j) {
            const StudySession& second = proposedSchedule.studySessions[j];
            if (occupiesTime(second.status)
                && first.time.overlaps(second.time)) {
                result.add({
                    ConstraintViolationCode::OverlapsStudySession,
                    "Sessions " + first.id + " and " + second.id
                        + " overlap",
                    first.taskId,
                    first.id
                });
            }
        }
    }

    for (const Task& task : tasks) {
        for (const TaskId& dependencyId : task.dependencies) {
            const Task* dependency = findTask(tasks, dependencyId);
            if (dependency == nullptr
                || dependency->status == TaskStatus::Done) {
                continue;
            }

            std::optional<TimePoint> dependencyEnd;
            std::optional<TimePoint> taskStart;
            for (const StudySession& session : proposedSchedule.studySessions) {
                if (!occupiesTime(session.status)) {
                    continue;
                }
                if (session.taskId == dependencyId) {
                    dependencyEnd = dependencyEnd.has_value()
                        ? std::max(*dependencyEnd, session.time.end)
                        : session.time.end;
                }
                if (session.taskId == task.id) {
                    taskStart = taskStart.has_value()
                        ? std::min(*taskStart, session.time.start)
                        : session.time.start;
                }
            }

            if (dependencyEnd.has_value() && taskStart.has_value()
                && *taskStart < *dependencyEnd) {
                result.add({
                    ConstraintViolationCode::DependencyOrderViolation,
                    "Task " + task.id + " starts before dependency "
                        + dependencyId + " finishes",
                    task.id,
                    std::nullopt
                });
            }
        }
    }

    return result;
}

ValidationResult ConstraintValidator::validateDependencies(
    std::span<const Task> tasks
) const {
    ValidationResult result;
    std::unordered_map<TaskId, const Task*> byId;

    for (const Task& task : tasks) {
        if (task.id.empty()) {
            result.add({
                ConstraintViolationCode::InvalidTaskId,
                "Task ID cannot be empty",
                task.id,
                std::nullopt
            });
            continue;
        }

        if (!byId.emplace(task.id, &task).second) {
            result.add({
                ConstraintViolationCode::DuplicateId,
                "Duplicate task ID: " + task.id,
                task.id,
                std::nullopt
            });
        }
    }

    for (const Task& task : tasks) {
        for (const TaskId& dependencyId : task.dependencies) {
            if (dependencyId == task.id) {
                result.add({
                    ConstraintViolationCode::DependencyCycle,
                    "Task cannot depend on itself: " + task.id,
                    task.id,
                    std::nullopt
                });
            } else if (!byId.contains(dependencyId)) {
                result.add({
                    ConstraintViolationCode::MissingDependency,
                    "Missing dependency " + dependencyId
                        + " for task " + task.id,
                    task.id,
                    std::nullopt
                });
            }
        }
    }

    enum class Mark { White, Gray, Black };
    std::unordered_map<TaskId, Mark> marks;
    std::function<bool(const Task&)> visit = [&](const Task& task) {
        Mark& mark = marks[task.id];
        if (mark == Mark::Gray) {
            return false;
        }
        if (mark == Mark::Black) {
            return true;
        }

        mark = Mark::Gray;
        for (const TaskId& dependencyId : task.dependencies) {
            const auto it = byId.find(dependencyId);
            if (it != byId.end() && !visit(*it->second)) {
                return false;
            }
        }
        mark = Mark::Black;
        return true;
    };

    for (const Task& task : tasks) {
        if (!task.id.empty() && !visit(task)) {
            result.add({
                ConstraintViolationCode::DependencyCycle,
                "Dependency cycle detected near task " + task.id,
                task.id,
                std::nullopt
            });
            break;
        }
    }

    return result;
}

ValidationResult ConstraintValidator::validateProtectedEntriesUnchanged(
    const Schedule& currentSchedule,
    const Schedule& proposedSchedule
) const {
    ValidationResult result;

    for (const FixedEvent& current : currentSchedule.fixedEvents) {
        const auto it = std::find_if(
            proposedSchedule.fixedEvents.begin(),
            proposedSchedule.fixedEvents.end(),
            [&](const FixedEvent& proposed) {
                return proposed.id == current.id;
            }
        );

        if (it == proposedSchedule.fixedEvents.end()
            || !sameFixedEvent(current, *it)) {
            result.add({
                ConstraintViolationCode::FixedEventModified,
                "Fixed event was removed or modified: " + current.id,
                std::nullopt,
                current.id
            });
        }
    }

    for (const StudySession& current : currentSchedule.studySessions) {
        if (!current.isLocked) {
            continue;
        }

        const auto it = std::find_if(
            proposedSchedule.studySessions.begin(),
            proposedSchedule.studySessions.end(),
            [&](const StudySession& proposed) {
                return proposed.id == current.id;
            }
        );

        if (it == proposedSchedule.studySessions.end()
            || !sameStudySession(current, *it)) {
            result.add({
                ConstraintViolationCode::LockedSessionModified,
                "Locked session was removed or modified: " + current.id,
                current.taskId,
                current.id
            });
        }
    }

    return result;
}

const Task* ConstraintValidator::findTask(
    std::span<const Task> tasks,
    std::string_view taskId
) const noexcept {
    const auto it = std::find_if(
        tasks.begin(),
        tasks.end(),
        [&](const Task& task) {
            return task.id == taskId;
        }
    );

    return it == tasks.end() ? nullptr : &*it;
}

} // namespace edu_schedule
