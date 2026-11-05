#pragma once

#include "agent/Tool.h"
#include "scheduler/scheduler.h"
#include "tools/ScheduleProposalStore.h"

#include <memory>
#include <vector>

namespace edu_schedule {

// Tool 6: bọc Scheduler để Agent yêu cầu một lịch xem trước.
class ScheduleSolverTool final : public ITool {
public:
    ScheduleSolverTool(const Scheduler& scheduler,
                       std::vector<Task> tasks,
                       Schedule currentSchedule,
                       TimePoint now,
                       TimePoint planningEnd,
                       std::shared_ptr<ScheduleProposalStore> proposalStore = nullptr);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    ToolResult execute(std::string_view input) override;

private:
    // Scheduler phải sống lâu hơn Tool; dữ liệu task và lịch được Tool sở hữu.
    const Scheduler& scheduler_;
    std::vector<Task> tasks_;
    Schedule currentSchedule_;
    TimePoint now_;
    TimePoint planningEnd_;
    std::shared_ptr<ScheduleProposalStore> proposalStore_;
};

} // namespace edu_schedule
