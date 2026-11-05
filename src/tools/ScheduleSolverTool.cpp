#include "tools/ScheduleSolverTool.h"

#include <chrono>
#include <sstream>
#include <utility>

namespace edu_schedule {

ScheduleSolverTool::ScheduleSolverTool(
    const Scheduler& scheduler,
    std::vector<Task> tasks,
    Schedule currentSchedule,
    TimePoint now,
    TimePoint planningEnd,
    std::shared_ptr<ScheduleProposalStore> proposalStore
) : scheduler_(scheduler),
    tasks_(std::move(tasks)),
    currentSchedule_(std::move(currentSchedule)),
    now_(now),
    planningEnd_(planningEnd),
    proposalStore_(std::move(proposalStore)) {}

std::string_view ScheduleSolverTool::name() const noexcept {
    return "schedule_solver";
}

std::string_view ScheduleSolverTool::description() const noexcept {
    return "Create a study schedule proposal";
}

ToolResult ScheduleSolverTool::execute(std::string_view input) {
    // Mỗi lượt bắt đầu với khay rỗng để Tool 7 không đọc nhầm lịch cũ.
    if (proposalStore_) {
        proposalStore_->latestProposal.reset();
    }
    if (input != "{}" && input != "preview") {
        return {false, "Expected command: preview"};
    }

    // Scheduler chỉ tạo bản đề xuất, không ghi lịch thật.
    const SchedulingResult result = scheduler_.createProposal(
        {tasks_, currentSchedule_, now_, planningEnd_}
    );
    if (proposalStore_) {
        proposalStore_->latestProposal = result;
    }

    std::ostringstream output;
    output << "Proposed study sessions: "
           << result.proposedSchedule.studySessions.size() << '\n';
    for (const StudySession& session : result.proposedSchedule.studySessions) {
        const auto start = std::chrono::duration_cast<std::chrono::seconds>(
            session.time.start.time_since_epoch()).count();
        const auto end = std::chrono::duration_cast<std::chrono::seconds>(
            session.time.end.time_since_epoch()).count();
        output << session.id << ": task " << session.taskId
               << ", start_utc_seconds=" << start
               << ", end_utc_seconds=" << end << '\n';
    }
    for (const std::string& reason : result.reasons) {
        output << "Reason: " << reason << '\n';
    }

    const bool success = result.status == SchedulingStatus::Scheduled
        || result.status == SchedulingStatus::PartiallyScheduled;
    return {success, output.str()};
}

} // namespace edu_schedule
