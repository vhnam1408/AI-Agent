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

// QUY ƯỚC BẮT BUỘC:
// - Đây là kết quả proposal, chưa phải dữ liệu đã commit.
// - Scheduled: xếp đủ; PartiallyScheduled: còn phần chưa xếp.
// - Infeasible: không có phương án hợp lệ; NeedsConfirmation: cần làm rõ.
// - Mọi proposal đều phải đi qua Preview -> Confirm -> Commit.
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
    // Chỉ bỏ qua chính phiên có ID hợp lệ. Validator vẫn phải phát hiện ID trùng.
    if (!first.id.empty() && first.id == second.id) {
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
