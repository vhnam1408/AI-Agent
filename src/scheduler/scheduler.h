#pragma once

#include "scheduler/constraint_validator.h"
#include "scheduler/priority_strategy.h"
#include "tools/Task_domain.h"

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - Header này chốt giao diện; thuật toán Scheduler sẽ triển khai ở giai đoạn sau.
// - Scheduler chỉ tạo proposal trong bộ nhớ, không gọi LLM/Tool và không ghi DB.
// - Chỉ gọi Scheduler khi người dùng yêu cầu xếp/phục hồi lịch. Việc khai báo
//   Task hoặc deadline riêng lẻ không tự động tạo StudySession.

struct SchedulerConfig {
    // Bước tìm kiếm slot; không làm tròn deadline hoặc FixedEvent để tạo chỗ.
    std::chrono::minutes slotGranularity{30};

    // Soft constraint: ưu tiên phiên <= 120 phút, tránh vượt 180 phút.
    // maximumSessionLength là heuristic, không được biến thành hard constraint.
    std::chrono::minutes preferredSessionLength{120};
    std::chrono::minutes maximumSessionLength{180};

    [[nodiscard]]
    bool isValid() const noexcept {
        return slotGranularity > std::chrono::minutes{0}
            && preferredSessionLength > std::chrono::minutes{0}
            && maximumSessionLength >= preferredSessionLength;
    }
};

// tasks/span và currentSchedule là dữ liệu mượn, phải sống hết lời gọi đồng bộ.
// Không lưu span/reference vào Scheduler. now/planningEnd là TimePoint tuyệt đối.
// Phạm vi [now, planningEnd) phải hữu hạn và planningEnd > now.
// Task không deadline vẫn dùng phạm vi này; không tự đặt deadline giả cho Task.
struct SchedulingRequest {
    std::span<const Task> tasks;
    const Schedule& currentSchedule;
    TimePoint now;
    TimePoint planningEnd;
};

// Điều kiện thêm: missedSessionId phải trỏ tới phiên có status == Missed.
struct RecoveryRequest {
    std::span<const Task> tasks;
    const Schedule& currentSchedule;
    std::string missedSessionId;
    TimePoint now;
    TimePoint planningEnd;
};

class Scheduler {
public:
    // Khi triển khai: dependency nullptr hoặc config sai -> invalid_argument.
    // Lỗi dữ liệu đầu vào thông thường phải trả SchedulingResult có lý do.
    Scheduler(
        std::shared_ptr<const ConstraintValidator> validator,
        std::shared_ptr<const PriorityStrategy> priorityStrategy,
        SchedulerConfig config = {}
    );

    // Giữ nguyên currentSchedule; kiểm tra hard constraints trước heuristics.
    // Xếp remainingMinutes và trừ thời lượng Planned còn giữ để tránh xếp đôi.
    // Done/Cancelled không xếp mới; Blocked phải nêu lý do nếu chưa giải tỏa.
    // Thiếu chỗ -> PartiallyScheduled/Infeasible, không âm thầm bỏ Task.
    [[nodiscard]]
    SchedulingResult createProposal(
        const SchedulingRequest& request
    ) const;

    // remainingMinutes phải phản ánh tiến độ; không cộng đôi phút của phiên lỡ.
    // Giữ phiên Missed làm lịch sử, chỉ tạo phiên bù tương lai.
    // Không thay FixedEvent/phiên khóa; kết quả vẫn cần confirm trước commit.
    [[nodiscard]]
    SchedulingResult createRecoveryProposal(
        const RecoveryRequest& request
    ) const;

    [[nodiscard]]
    const SchedulerConfig& config() const noexcept;

private:
    [[nodiscard]]
    std::vector<const Task*> topologicalOrder(
        std::span<const Task> tasks,
        std::vector<std::string>& reasons
    ) const;

    [[nodiscard]]
    std::vector<const Task*> rankReadyTasks(
        std::span<const Task* const> tasks,
        TimePoint now
    ) const;

    [[nodiscard]]
    std::vector<TimeSlot> findAvailableSlots(
        const Schedule& schedule,
        TimePoint from,
        TimePoint until
    ) const;

    [[nodiscard]]
    SchedulingResult makeFailure(
        SchedulingStatus status,
        const Schedule& unchangedSchedule,
        std::vector<std::string> reasons
    ) const;

    std::shared_ptr<const ConstraintValidator> validator_;
    std::shared_ptr<const PriorityStrategy> priorityStrategy_;
    SchedulerConfig config_;
};

} // namespace edu_schedule
