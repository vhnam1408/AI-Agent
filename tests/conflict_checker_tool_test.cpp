#include "agent/ToolRegistry.h"
#include "tools/ConflictCheckerTool.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace edu_schedule;
using namespace std::chrono;
using namespace std::chrono_literals;

int main() {
    const TimePoint now = sys_days{2026y/November/5} + 1h;
    ConstraintValidator validator;
    std::vector<Task> tasks{
        {"T1", "Bai tap OOP", "OOP", now + 10h, 60, 60,
         Priority::High, TaskStatus::Todo, {}}
    };
    Schedule currentSchedule;
    currentSchedule.fixedEvents.push_back({
        "class-1", "Lop hoc", {now + 1h, now + 3h}
    });

    // 11:00-12:00 nằm ngay sau lớp 09:00-11:00: không xung đột.
    Schedule validSchedule = currentSchedule;
    validSchedule.studySessions.push_back({
        "session-1", "T1", {now + 3h, now + 4h},
        false, SessionStatus::Planned
    });
    ToolRegistry registry;
    registry.registerTool(std::make_unique<ConflictCheckerTool>(
        validator, tasks, currentSchedule, validSchedule, now));
    registry.allowTool("conflict_checker");
    const ToolResult valid = registry.execute("conflict_checker", "{}");

    // 10:00-11:00 đè lên lớp nên phải bị từ chối.
    Schedule overlapping = currentSchedule;
    overlapping.studySessions.push_back({
        "session-2", "T1", {now + 2h, now + 3h},
        false, SessionStatus::Planned
    });
    ConflictCheckerTool conflictTool(
        validator, tasks, currentSchedule, overlapping, now);
    const ToolResult conflict = conflictTool.execute("check");

    // Hai phiên 2 giờ trong cùng ngày phải cho điểm quá tải 50/100.
    std::vector<Task> busyTasks = tasks;
    busyTasks[0].estimatedMinutes = 240;
    busyTasks[0].remainingMinutes = 240;
    Schedule busy = currentSchedule;
    busy.studySessions.push_back({
        "busy-1", "T1", {now + 3h, now + 5h},
        false, SessionStatus::Planned
    });
    busy.studySessions.push_back({
        "busy-2", "T1", {now + 5h, now + 7h},
        false, SessionStatus::Planned
    });
    ConflictCheckerTool busyTool(validator, busyTasks, currentSchedule, busy, now);
    const ToolResult burnout = busyTool.execute("check");

    if (!valid.success || conflict.success || !burnout.success
        || burnout.message.find("50/100") == std::string::npos) {
        std::cerr << "Conflict or burnout check failed\n";
        return 1;
    }
    std::cout << valid.message << '\n' << conflict.message
              << burnout.message << '\n';
    return 0;
}
