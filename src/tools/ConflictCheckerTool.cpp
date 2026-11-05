#include "tools/ConflictCheckerTool.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <utility>

namespace edu_schedule {

ConflictCheckerTool::ConflictCheckerTool(
    const ConstraintValidator& validator,
    std::vector<Task> tasks,
    Schedule currentSchedule,
    Schedule proposedSchedule,
    TimePoint now,
    std::shared_ptr<ScheduleProposalStore> proposalStore
) : validator_(validator),
    tasks_(std::move(tasks)),
    currentSchedule_(std::move(currentSchedule)),
    proposedSchedule_(std::move(proposedSchedule)),
    now_(now),
    proposalStore_(std::move(proposalStore)) {}

std::string_view ConflictCheckerTool::name() const noexcept {
    return "conflict_checker";
}

std::string_view ConflictCheckerTool::description() const noexcept {
    return "Check schedule conflicts and estimate burnout";
}

ToolResult ConflictCheckerTool::execute(std::string_view input) {
    if (input != "{}" && input != "check") {
        return {false, "Expected command: check"};
    }

    // Khi dùng chung khay, kiểm tra đúng bản lịch Tool 6 vừa tạo.
    if (proposalStore_) {
        const auto& latest = proposalStore_->latestProposal;
        if (!latest || latest->status == SchedulingStatus::Infeasible
            || latest->status == SchedulingStatus::NeedsConfirmation) {
            return {false, "Run schedule_solver before conflict_checker"};
        }
        proposedSchedule_ = latest->proposedSchedule;
    }

    const ValidationResult validation = validator_.validateSchedule(
        proposedSchedule_, currentSchedule_, tasks_, now_
    );
    if (!validation.isValid()) {
        std::ostringstream output;
        for (const ConstraintViolation& violation : validation.violations) {
            output << "Conflict: " << violation.message << '\n';
        }
        return {false, output.str()};
    }

    return {true, "No schedule conflicts found. Burnout score: "
        + std::to_string(calculateBurnoutScore()) + "/100"};
}

int ConflictCheckerTool::calculateBurnoutScore() const {
    // Cộng các phiên Planned theo ngày Việt Nam (UTC+07).
    std::map<std::chrono::sys_days, long long> minutesByDay;
    for (const StudySession& session : proposedSchedule_.studySessions) {
        if (session.status != SessionStatus::Planned) {
            continue;
        }
        const auto localDay = std::chrono::floor<std::chrono::days>(
            session.time.start + std::chrono::hours{7});
        minutesByDay[localDay] += std::chrono::duration_cast<std::chrono::minutes>(
            session.time.end - session.time.start).count();
    }

    long long busiestDayMinutes = 0;
    for (const auto& [day, minutes] : minutesByDay) {
        (void)day;
        busiestDayMinutes = std::max(busiestDayMinutes, minutes);
    }

    // 2 giờ = 0 điểm, 4 giờ = 50 điểm, từ 6 giờ = 100 điểm.
    const long long bounded = std::clamp(busiestDayMinutes, 120LL, 360LL);
    return static_cast<int>((bounded - 120) * 100 / 240);
}

} // namespace edu_schedule
