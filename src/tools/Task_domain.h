
#pragma once

#include "../models/Task.h"
#include "../models/Priority.h"
#include "../models/TaskStatus.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace edu {

using TaskId = std::string;
using TimePoint = std::chrono::system_clock::time_point;

struct TimeSlot {
    TimePoint start;
    TimePoint end;

    [[nodiscard]]
    bool isValid() const noexcept {
        return start < end;
    }

    [[nodiscard]]
    bool overlaps(const TimeSlot& other) const noexcept {
        return start < other.end && other.start < end;
    }
};

// Sự kiện chiếm thời gian cố định, ví dụ giờ học hoặc cuộc họp.
struct FixedEvent {
    std::string id;
    std::string title;
    TimeSlot time;
};

// Phiên học gắn với một Task.
enum class SessionStatus {
    Planned,
    Completed,
    Missed,
    Cancelled
};

struct StudySession {
    std::string id;
    TaskId taskId;
    TimeSlot time;
    bool isLocked = false;
    SessionStatus status = SessionStatus::Planned;
};

// Lịch tổng hợp các sự kiện và phiên học.
struct Schedule {
    std::vector<FixedEvent> fixedEvents;
    std::vector<StudySession> studySessions;
};

// Kết quả lập lịch theo quy tắc trong tài liệu.
enum class SchedulingStatus {
    Scheduled,
    PartiallyScheduled,
    Infeasible,
    NeedsConfirmation
};

struct SchedulingResult {
    SchedulingStatus status = SchedulingStatus::Infeasible;
    Schedule proposedSchedule;
    std::vector<std::string> reasons;
};

// Kiểm tra phiên học có đè lên sự kiện cố định hay không.
[[nodiscard]]
inline bool conflictsWithFixedEvent(
    const StudySession& session,
    const FixedEvent& event
) noexcept {
    return session.time.overlaps(event.time);
}

// Kiểm tra phiên học có trùng với phiên học khác hay không.
[[nodiscard]]
inline bool conflictsWithSession(
    const StudySession& first,
    const StudySession& second
) noexcept {
    // Không xem chính phiên đó là xung đột.
    if (first.id == second.id) {
        return false;
    }

    return first.time.overlaps(second.time);
}

[[nodiscard]]
inline bool isValidTaskId(std::string_view id) noexcept {
    return !id.empty();
}

[[nodiscard]]
inline bool isValidDuration(int minutes) noexcept {
    return minutes > 0;
}

} // namespace edu
