#pragma once

#include "tools/Task_domain.h"

#include <optional>

namespace edu_schedule {

// Bản xem trước dùng chung trong một lượt gọi Tool; không ghi vào lịch thật.
struct ScheduleProposalStore {
    std::optional<SchedulingResult> latestProposal;
};

} // namespace edu_schedule
