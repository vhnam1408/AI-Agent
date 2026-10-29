#include "scheduler/scheduler.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace edu_schedule;
using namespace std::chrono_literals;

namespace {

TimePoint localTime(
    int year,
    unsigned month,
    unsigned day,
    int hour,
    int minute = 0
) {
    const std::chrono::sys_days localDay{
        std::chrono::year{year}
        / std::chrono::month{month}
        / std::chrono::day{day}
    };

    // Dữ liệu nhập là giờ Việt Nam UTC+07; TimePoint lưu mốc UTC tuyệt đối.
    return localDay
        + std::chrono::hours{hour}
        + std::chrono::minutes{minute}
        - 7h;
}

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int minutes(const TimeSlot& slot) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::minutes>(
            slot.end - slot.start
        ).count()
    );
}

} // namespace

int main() {
    const TimePoint now = localTime(2026, 10, 29, 8);
    const TimePoint planningEnd = localTime(2026, 10, 30, 22);

    std::vector<Task> tasks{
        {
            "T1", "OOP design", "OOP",
            localTime(2026, 10, 29, 20),
            120, 120, Priority::High, TaskStatus::Todo, {}
        },
        {
            "T2", "AI exercises", "AI",
            localTime(2026, 10, 30, 18),
            90, 90, Priority::Mid, TaskStatus::Todo, {}
        },
        {
            "T3", "Database report", "DB",
            localTime(2026, 10, 30, 20),
            60, 60, Priority::Low, TaskStatus::Todo, {}
        },
        {
            "T4", "Implement OOP code", "OOP",
            localTime(2026, 10, 30, 21),
            120, 120, Priority::High, TaskStatus::Todo, {"T1"}
        },
        {
            "T5", "Review computer networks", "CN",
            std::nullopt,
            90, 90, Priority::Mid, TaskStatus::Todo, {}
        }
    };

    Schedule currentSchedule;
    currentSchedule.fixedEvents = {
        {
            "class-morning", "Morning class",
            {
                localTime(2026, 10, 29, 9),
                localTime(2026, 10, 29, 11)
            }
        },
        {
            "class-afternoon", "Afternoon class",
            {
                localTime(2026, 10, 29, 14),
                localTime(2026, 10, 29, 16)
            }
        }
    };

    auto validator = std::make_shared<ConstraintValidator>();
    auto strategy = std::make_shared<UrgencySlackPriorityStrategy>();
    Scheduler scheduler(validator, strategy);

    const SchedulingResult result = scheduler.createProposal({
        tasks,
        currentSchedule,
        now,
        planningEnd
    });

    check(
        result.status == SchedulingStatus::Scheduled,
        "all five tasks should be scheduled"
    );

    check(
        result.proposedSchedule.fixedEvents.size()
            == currentSchedule.fixedEvents.size(),
        "fixed events must be preserved"
    );

    std::unordered_map<TaskId, int> scheduledMinutes;

    for (const StudySession& session :
         result.proposedSchedule.studySessions) {
        check(session.time.isValid(), "session has invalid time range");
        check(session.time.start >= now, "session starts in the past");
        check(
            session.time.end <= planningEnd,
            "session exceeds planning end"
        );

        const auto taskIt = std::find_if(
            tasks.begin(),
            tasks.end(),
            [&](const Task& task) {
                return task.id == session.taskId;
            }
        );

        check(
            taskIt != tasks.end(),
            "session references an unknown task"
        );

        if (taskIt->deadline.has_value()) {
            check(
                session.time.end <= *taskIt->deadline,
                "session exceeds task deadline"
            );
        }

        for (const FixedEvent& event : currentSchedule.fixedEvents) {
            check(
                !session.time.overlaps(event.time),
                "session overlaps a fixed event"
            );
        }

        scheduledMinutes[session.taskId] += minutes(session.time);
    }

    const auto& sessions = result.proposedSchedule.studySessions;
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        for (std::size_t j = i + 1; j < sessions.size(); ++j) {
            check(
                !sessions[i].time.overlaps(sessions[j].time),
                "two study sessions overlap"
            );
        }
    }

    for (const Task& task : tasks) {
        check(
            scheduledMinutes[task.id] == task.remainingMinutes,
            "task " + task.id + " has incorrect scheduled minutes"
        );
    }

    std::optional<TimePoint> t1End;
    std::optional<TimePoint> t4Start;
    for (const StudySession& session : sessions) {
        if (session.taskId == "T1") {
            t1End = t1End.has_value()
                ? std::max(*t1End, session.time.end)
                : session.time.end;
        }
        if (session.taskId == "T4") {
            t4Start = t4Start.has_value()
                ? std::min(*t4Start, session.time.start)
                : session.time.start;
        }
    }

    check(t1End.has_value(), "T1 has no session");
    check(t4Start.has_value(), "T4 has no session");
    check(
        *t4Start >= *t1End,
        "dependent task T4 starts before T1 finishes"
    );

    std::cout
        << "PASS: five tasks scheduled without overlap\n"
        << "Created sessions: " << sessions.size() << '\n';

    return EXIT_SUCCESS;
}
