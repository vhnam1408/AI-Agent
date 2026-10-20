#pragma once

#include "../models/Task.h"
#include "../models/TimePoint.h"

#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

using TaskId = std::string;

// QUY ƯỚC BẮT BUỘC: mọi khoảng thời gian là nửa mở [start, end) và start < end.
// Vì vậy 09:00-10:00 và 10:00-11:00 không chồng lấn.
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

// QUY ƯỚC BẮT BUỘC: Scheduler không được tự di chuyển, rút ngắn hoặc xóa
// FixedEvent. FixedEvent có thể nằm ngoài khung học 08:00-22:00.
struct FixedEvent {
    std::string id;
    std::string title;
    TimeSlot time;
};

// Trạng thái của phiên học, độc lập với TaskStatus.
enum class SessionStatus {
    Planned,
    Completed,
    Missed,
    Cancelled
};

struct StudySession {
    // id không rỗng, duy nhất; taskId phải trỏ tới Task tồn tại.
    // isLocked bảo vệ phiên khỏi thay đổi tự động; confirm không tự động khóa.
    // Planned chiếm chỗ. Missed/Cancelled không chiếm chỗ cho lịch mới nhưng
    // vẫn phải được giữ lại làm lịch sử.
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

enum class SchedulingStatus {
    Scheduled,
    PartiallyScheduled,
    Infeasible,
    NeedsConfirmation
};

struct SchedulingResult {
    SchedulingStatus status = SchedulingStatus::Infeasible;
    Schedule proposedSchedule;
    // Khi không xếp đủ phải nêu Task/phần thời lượng còn thiếu và nguyên nhân.
    std::vector<std::string> reasons;
};

[[nodiscard]]
inline bool conflictsWithFixedEvent(
    const StudySession& session,
    const FixedEvent& event
) noexcept {
    return session.time.overlaps(event.time);
}

[[nodiscard]]
inline bool conflictsWithSession(
    const StudySession& first,
    const StudySession& second
) noexcept {
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

} // namespace edu_schedule