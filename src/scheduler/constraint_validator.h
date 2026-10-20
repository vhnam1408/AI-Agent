#pragma once

#include "tools/Task_domain.h"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - start/end là giờ địa phương tính từ 00:00, không phải TimePoint.
// - MVP mặc định [08:00, 22:00], UTC+07:00 (Việt Nam, chưa hỗ trợ DST).
// - FixedEvent/deadline được giữ nguyên dù nằm ngoài khung học.
struct DailyStudyWindow {
    std::chrono::minutes start{std::chrono::hours{8}};
    std::chrono::minutes end{std::chrono::hours{22}};
    std::chrono::minutes utcOffset{std::chrono::hours{7}};

    [[nodiscard]]
    bool isValid() const noexcept {
        return start >= std::chrono::minutes{0}
            && start < end
            && end <= std::chrono::hours{24}
            && utcOffset >= -std::chrono::hours{12}
            && utcOffset <= std::chrono::hours{14};
    }
};

enum class ConstraintViolationCode {
    InvalidTaskId,
    DuplicateId,
    InvalidTimeRange,
    InvalidTaskDuration,
    OverlapsFixedEvent,
    OverlapsStudySession,
    OutsideStudyWindow,
    StartsInPast,
    EndsAfterDeadline,
    MissingTask,
    MissingDependency,
    DependencyOrderViolation,
    DependencyCycle,
    FixedEventModified,
    LockedSessionModified
};

struct ConstraintViolation {
    ConstraintViolationCode code;
    std::string message;
    std::optional<TaskId> taskId;
    std::optional<std::string> sessionId;
};

struct ValidationResult {
    std::vector<ConstraintViolation> violations;

    [[nodiscard]]
    bool isValid() const noexcept {
        return violations.empty();
    }

    void add(ConstraintViolation violation) {
        violations.push_back(std::move(violation));
    }
};

// QUY ƯỚC BẮT BUỘC:
// - Chỉ kiểm tra hard constraints, không chấm điểm và không sửa dữ liệu.
// - Đây là giao diện; phần kiểm tra đầy đủ sẽ triển khai ở giai đoạn sau.
// - Dữ liệu span/reference phải sống hết lời gọi.
class ConstraintValidator {
public:
    // Khi triển khai: studyWindow không hợp lệ -> std::invalid_argument.
    explicit ConstraintValidator(
        DailyStudyWindow studyWindow = {}
    );

    [[nodiscard]]
    const DailyStudyWindow& studyWindow() const noexcept;

    [[nodiscard]]
    ValidationResult validateTimeSlot(
        const TimeSlot& slot
    ) const;

    [[nodiscard]]
    // Phiên mới: start >= now, end <= deadline nếu có, nằm trong khung học,
    // không đè lên mục đang chiếm chỗ. Missed/Cancelled không chiếm slot mới.
    ValidationResult validateStudySession(
        const StudySession& session,
        const Schedule& occupiedSchedule,
        const Task& task,
        TimePoint now
    ) const;

    // So sánh proposal với currentSchedule để phân biệt lịch sử và phiên mới.
    // Quy tắc "không tạo trong quá khứ" không được làm mất lịch sử đã có.
    // Phải phát hiện ID trùng trước khi bỏ qua phép so sánh cùng ID.
    [[nodiscard]]
    ValidationResult validateSchedule(
        const Schedule& proposedSchedule,
        const Schedule& currentSchedule,
        std::span<const Task> tasks,
        TimePoint now
    ) const;

    // Phát hiện ID thiếu, tự phụ thuộc, dependency không tồn tại và chu trình.
    // Thứ tự thời gian giữa các phiên phụ thuộc kiểm tra trong validateSchedule().
    [[nodiscard]]
    ValidationResult validateDependencies(
        std::span<const Task> tasks
    ) const;

    // Preview -> Confirm -> Commit: proposal không được âm thầm di chuyển,
    // rút ngắn hoặc xóa FixedEvent/Locked StudySession.
    [[nodiscard]]
    ValidationResult validateProtectedEntriesUnchanged(
        const Schedule& currentSchedule,
        const Schedule& proposedSchedule
    ) const;

private:
    [[nodiscard]]
    const Task* findTask(
        std::span<const Task> tasks,
        std::string_view taskId
    ) const noexcept;

    DailyStudyWindow studyWindow_;
};

} // namespace edu_schedule
