#pragma once

#include "../models/Task.h"
#include "../models/Course.h"
#include "../models/domain_types.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace edu_schedule {

// Time intervals use half-open [start, end): adjacent intervals do not overlap.
struct TimeSlot {
    TimePoint start;
    TimePoint end;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool overlaps(const TimeSlot& other) const noexcept;
};

struct FixedEvent {
    std::string id;
    std::string title;
    TimeSlot time;
};

enum class SessionStatus { Planned, Completed, Missed, Cancelled };

struct StudySession {
    std::string id;
    TaskId taskId;
    TimeSlot time;
    bool isLocked = false;
    SessionStatus status = SessionStatus::Planned;
};

struct Schedule {
    std::vector<FixedEvent> fixedEvents;
    std::vector<StudySession> studySessions;
};

enum class SchedulingStatus { Scheduled, PartiallyScheduled, Infeasible, NeedsConfirmation };

struct SchedulingResult {
    SchedulingStatus status = SchedulingStatus::Infeasible;
    Schedule proposedSchedule;
    std::vector<std::string> reasons;
};

[[nodiscard]] bool conflictsWithFixedEvent(const StudySession& session, const FixedEvent& event) noexcept;
[[nodiscard]] bool conflictsWithSession(const StudySession& first, const StudySession& second) noexcept;
[[nodiscard]] bool isValidTaskId(std::string_view id) noexcept;
[[nodiscard]] bool isValidDuration(int minutes) noexcept;

// Owns Task and Course objects via unique_ptr. Returned pointers are borrowed;
// they remain valid until that item is erased or the domain is destroyed.
class TaskDomain {
public:
    TaskDomain() = default;
    TaskDomain(const TaskDomain&) = delete;
    TaskDomain& operator=(const TaskDomain&) = delete;
    TaskDomain(TaskDomain&&) noexcept = default;
    TaskDomain& operator=(TaskDomain&&) noexcept = default;

    bool addTask(std::unique_ptr<Task> task);
    bool addCourse(std::unique_ptr<Course> course);
    [[nodiscard]] Task* findTask(std::string_view id);
    [[nodiscard]] const Task* findTask(std::string_view id) const;
    [[nodiscard]] Course* findCourse(std::string_view id);
    [[nodiscard]] const Course* findCourse(std::string_view id) const;
    [[nodiscard]] bool removeTask(std::string_view id);
    [[nodiscard]] bool removeCourse(std::string_view id);
    [[nodiscard]] std::size_t taskCount() const noexcept;
    [[nodiscard]] std::size_t courseCount() const noexcept;

private:
    std::unordered_map<TaskId, std::unique_ptr<Task>> tasks_;
    std::unordered_map<std::string, std::unique_ptr<Course>> courses_;
};

} // namespace edu_schedule
