#pragma once

#include "agent/Tool.h"
#include "scheduler/constraint_validator.h"
#include "tools/ScheduleProposalStore.h"

#include <memory>
#include <vector>

namespace edu_schedule {

// Tool 7: kiểm tra lịch đề xuất và ước lượng tải học, không sửa lịch thật.
class ConflictCheckerTool final : public ITool {
public:
    ConflictCheckerTool(const ConstraintValidator& validator,
                        std::vector<Task> tasks,
                        Schedule currentSchedule,
                        Schedule proposedSchedule,
                        TimePoint now,
                        std::shared_ptr<ScheduleProposalStore> proposalStore = nullptr);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    ToolResult execute(std::string_view input) override;

private:
    [[nodiscard]] int calculateBurnoutScore() const;

    // Validator phải sống lâu hơn Tool; các Schedule bên dưới là bản sao.
    const ConstraintValidator& validator_;
    std::vector<Task> tasks_;
    Schedule currentSchedule_;
    Schedule proposedSchedule_;
    TimePoint now_;
    std::shared_ptr<ScheduleProposalStore> proposalStore_;
};

} // namespace edu_schedule
