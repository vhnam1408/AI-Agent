#include "agent/ToolRegistry.h"
#include "tools/ConflictCheckerTool.h"
#include "tools/ScheduleSolverTool.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

using namespace edu_schedule;
using namespace std::chrono;
using namespace std::chrono_literals;

int main() {
    // 01:00 UTC là 08:00 tại Việt Nam.
    const TimePoint now = sys_days{2026y/November/5} + 1h;
    std::vector<Task> tasks{
        {"T1", "Bai tap OOP", "OOP", now + 10h, 60, 60,
         Priority::High, TaskStatus::Todo, {}}
    };
    Schedule currentSchedule;

    auto validator = std::make_shared<ConstraintValidator>();
    auto strategy = std::make_shared<UrgencySlackPriorityStrategy>();
    Scheduler scheduler(validator, strategy);
    auto proposalStore = std::make_shared<ScheduleProposalStore>();

    ToolRegistry registry;
    registry.registerTool(std::make_unique<ScheduleSolverTool>(
        scheduler, tasks, currentSchedule, now, now + 14h, proposalStore));
    registry.registerTool(std::make_unique<ConflictCheckerTool>(
        *validator, tasks, currentSchedule, Schedule{}, now, proposalStore));
    registry.allowTool("schedule_solver");
    registry.allowTool("conflict_checker");

    // Tool 7 phải từ chối khi Tool 6 chưa tạo bản xem trước.
    const ToolResult before = registry.execute("conflict_checker", "{}");
    const ToolResult solved = registry.execute("schedule_solver", "{}");
    const ToolResult checked = registry.execute("conflict_checker", "{}");
    if (before.success || !solved.success || !checked.success
        || !proposalStore->latestProposal.has_value()
        || proposalStore->latestProposal->proposedSchedule.studySessions.empty()) {
        std::cerr << "Tool 6 -> proposal store -> Tool 7 failed\n";
        return 1;
    }
    std::cout << solved.message << checked.message << '\n';
    return 0;
}
