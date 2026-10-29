#include "scheduler/scheduler.h"

#include <algorithm>
#include <array>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace edu_schedule {
namespace {

bool occupiesTime(SessionStatus status) noexcept {
    return status == SessionStatus::Planned
        || status == SessionStatus::Completed;
}

int slotMinutes(const TimeSlot& slot) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::minutes>(
            slot.end - slot.start
        ).count()
    );
}

void appendViolationReasons(
    std::vector<std::string>& reasons,
    const ValidationResult& validation
) {
    for (const ConstraintViolation& violation : validation.violations) {
        reasons.push_back(violation.message);
    }
}

TimePoint alignUp(
    TimePoint point,
    std::chrono::minutes granularity
) {
    const auto minutePoint = std::chrono::time_point_cast<std::chrono::minutes>(
        point
    );
    const auto count = minutePoint.time_since_epoch().count();
    const auto step = granularity.count();
    const auto aligned = ((count + step - 1) / step) * step;
    TimePoint result{std::chrono::minutes{aligned}};
    if (result < point) {
        result += granularity;
    }
    return result;
}

std::string makeSessionId(
    const TaskId& taskId,
    const Schedule& schedule
) {
    std::unordered_set<std::string> used;
    for (const StudySession& session : schedule.studySessions) {
        used.insert(session.id);
    }

    for (std::size_t index = 1;; ++index) {
        std::string candidate = "session-" + taskId + "-"
            + std::to_string(index);
        if (!used.contains(candidate)) {
            return candidate;
        }
    }
}

} // namespace

Scheduler::Scheduler(
    std::shared_ptr<const ConstraintValidator> validator,
    std::shared_ptr<const PriorityStrategy> priorityStrategy,
    SchedulerConfig config
) : validator_(std::move(validator)),
    priorityStrategy_(std::move(priorityStrategy)),
    config_(config) {
    if (!validator_ || !priorityStrategy_) {
        throw std::invalid_argument(
            "Scheduler dependencies cannot be null"
        );
    }
    if (!config_.isValid()) {
        throw std::invalid_argument("Invalid scheduler configuration");
    }
}

SchedulingResult Scheduler::createProposal(
    const SchedulingRequest& request
) const {
    if (request.planningEnd <= request.now) {
        return makeFailure(
            SchedulingStatus::Infeasible,
            request.currentSchedule,
            {"planningEnd must be later than now"}
        );
    }

    const ValidationResult dependencyValidation =
        validator_->validateDependencies(request.tasks);
    if (!dependencyValidation.isValid()) {
        std::vector<std::string> reasons;
        appendViolationReasons(reasons, dependencyValidation);
        return makeFailure(
            SchedulingStatus::Infeasible,
            request.currentSchedule,
            std::move(reasons)
        );
    }

    Schedule proposal = request.currentSchedule;
    std::vector<std::string> reasons;
    std::unordered_map<TaskId, int> minutesNeeded;
    std::unordered_set<TaskId> satisfied;
    std::vector<const Task*> pending;

    for (const Task& task : request.tasks) {
        if (task.status == TaskStatus::Done) {
            satisfied.insert(task.id);
            continue;
        }
        if (task.status == TaskStatus::Cancelled) {
            continue;
        }
        if (task.status == TaskStatus::Blocked) {
            reasons.push_back("Task " + task.id + " is blocked");
            continue;
        }
        if (task.estimatedMinutes <= 0
            || task.remainingMinutes < 0
            || task.remainingMinutes > task.estimatedMinutes) {
            reasons.push_back(
                "Task " + task.id + " has invalid or unknown duration"
            );
            continue;
        }

        int plannedMinutes = 0;
        for (const StudySession& session : proposal.studySessions) {
            if (session.taskId == task.id
                && session.status == SessionStatus::Planned
                && session.time.end > request.now) {
                plannedMinutes += slotMinutes(session.time);
            }
        }

        const int needed = std::max(
            0,
            task.remainingMinutes - plannedMinutes
        );
        minutesNeeded.emplace(task.id, needed);
        if (needed == 0) {
            satisfied.insert(task.id);
        } else {
            pending.push_back(&task);
        }
    }

    bool addedAnySession = false;
    bool failedToPlaceAnyTask = false;

    while (!pending.empty()) {
        std::vector<const Task*> ready;
        for (const Task* task : pending) {
            const bool dependenciesReady = std::all_of(
                task->dependencies.begin(),
                task->dependencies.end(),
                [&](const TaskId& dependencyId) {
                    return satisfied.contains(dependencyId);
                }
            );
            if (dependenciesReady) {
                ready.push_back(task);
            }
        }

        if (ready.empty()) {
            failedToPlaceAnyTask = true;
            for (const Task* task : pending) {
                reasons.push_back(
                    "Task " + task->id
                    + " is waiting for an unfinished dependency"
                );
            }
            break;
        }

        ready = rankReadyTasks(ready, request.now);
        const Task* task = ready.front();
        int remaining = minutesNeeded.at(task->id);

        TimePoint earliestStart = request.now;
        for (const TaskId& dependencyId : task->dependencies) {
            for (const StudySession& session : proposal.studySessions) {
                if (session.taskId == dependencyId
                    && occupiesTime(session.status)) {
                    earliestStart = std::max(
                        earliestStart,
                        session.time.end
                    );
                }
            }
        }

        TimePoint latestEnd = request.planningEnd;
        if (task->deadline.has_value()) {
            latestEnd = std::min(latestEnd, *task->deadline);
        }

        if (latestEnd > earliestStart) {
            const std::vector<TimeSlot> slots = findAvailableSlots(
                proposal,
                earliestStart,
                latestEnd
            );

            for (const TimeSlot& slot : slots) {
                TimePoint cursor = slot.start;
                while (remaining > 0 && cursor < slot.end) {
                    const int capacity = static_cast<int>(
                        std::chrono::duration_cast<std::chrono::minutes>(
                            slot.end - cursor
                        ).count()
                    );
                    if (capacity <= 0) {
                        break;
                    }

                    const int chunk = std::min({
                        remaining,
                        capacity,
                        static_cast<int>(
                            config_.preferredSessionLength.count()
                        )
                    });

                    StudySession candidate{
                        makeSessionId(task->id, proposal),
                        task->id,
                        {cursor, cursor + std::chrono::minutes{chunk}},
                        false,
                        SessionStatus::Planned
                    };

                    const ValidationResult validation =
                        validator_->validateStudySession(
                            candidate,
                            proposal,
                            *task,
                            request.now
                        );
                    if (!validation.isValid()) {
                        appendViolationReasons(reasons, validation);
                        break;
                    }

                    proposal.studySessions.push_back(candidate);
                    cursor = candidate.time.end;
                    remaining -= chunk;
                    addedAnySession = true;
                }
                if (remaining == 0) {
                    break;
                }
            }
        }

        if (remaining == 0) {
            satisfied.insert(task->id);
        } else {
            failedToPlaceAnyTask = true;
            reasons.push_back(
                "Task " + task->id + " still needs "
                + std::to_string(remaining) + " minutes"
            );
        }

        pending.erase(
            std::remove(pending.begin(), pending.end(), task),
            pending.end()
        );
    }

    const ValidationResult finalValidation = validator_->validateSchedule(
        proposal,
        request.currentSchedule,
        request.tasks,
        request.now
    );
    if (!finalValidation.isValid()) {
        appendViolationReasons(reasons, finalValidation);
        return makeFailure(
            SchedulingStatus::Infeasible,
            request.currentSchedule,
            std::move(reasons)
        );
    }

    SchedulingStatus status = SchedulingStatus::Scheduled;
    if (failedToPlaceAnyTask || !reasons.empty()) {
        status = addedAnySession
            ? SchedulingStatus::PartiallyScheduled
            : SchedulingStatus::Infeasible;
    }

    return {status, std::move(proposal), std::move(reasons)};
}

SchedulingResult Scheduler::createRecoveryProposal(
    const RecoveryRequest& request
) const {
    const auto missedIt = std::find_if(
        request.currentSchedule.studySessions.begin(),
        request.currentSchedule.studySessions.end(),
        [&](const StudySession& session) {
            return session.id == request.missedSessionId;
        }
    );

    if (missedIt == request.currentSchedule.studySessions.end()
        || missedIt->status != SessionStatus::Missed) {
        return makeFailure(
            SchedulingStatus::Infeasible,
            request.currentSchedule,
            {"missedSessionId must reference a Missed session"}
        );
    }

    const auto taskIt = std::find_if(
        request.tasks.begin(),
        request.tasks.end(),
        [&](const Task& task) {
            return task.id == missedIt->taskId;
        }
    );
    if (taskIt == request.tasks.end()) {
        return makeFailure(
            SchedulingStatus::Infeasible,
            request.currentSchedule,
            {"Missed session references a missing task"}
        );
    }

    for (const TaskId& dependencyId : taskIt->dependencies) {
        const auto dependencyIt = std::find_if(
            request.tasks.begin(),
            request.tasks.end(),
            [&](const Task& task) {
                return task.id == dependencyId;
            }
        );
        if (dependencyIt == request.tasks.end()
            || dependencyIt->status != TaskStatus::Done) {
            return makeFailure(
                SchedulingStatus::Infeasible,
                request.currentSchedule,
                {"Recovery task has an unfinished dependency"}
            );
        }
    }

    Task recoveryTask = *taskIt;
    recoveryTask.dependencies.clear();
    const std::array<Task, 1> recoveryTasks{recoveryTask};

    return createProposal({
        recoveryTasks,
        request.currentSchedule,
        request.now,
        request.planningEnd
    });
}

const SchedulerConfig& Scheduler::config() const noexcept {
    return config_;
}

std::vector<const Task*> Scheduler::topologicalOrder(
    std::span<const Task> tasks,
    std::vector<std::string>& reasons
) const {
    std::unordered_map<TaskId, const Task*> byId;
    std::unordered_map<TaskId, int> indegree;
    std::unordered_map<TaskId, std::vector<TaskId>> dependents;

    for (const Task& task : tasks) {
        byId[task.id] = &task;
        indegree[task.id] = 0;
    }
    for (const Task& task : tasks) {
        for (const TaskId& dependencyId : task.dependencies) {
            if (!byId.contains(dependencyId)) {
                reasons.push_back(
                    "Missing dependency " + dependencyId
                    + " for task " + task.id
                );
                continue;
            }
            ++indegree[task.id];
            dependents[dependencyId].push_back(task.id);
        }
    }

    std::deque<TaskId> queue;
    for (const auto& [taskId, degree] : indegree) {
        if (degree == 0) {
            queue.push_back(taskId);
        }
    }

    std::vector<const Task*> order;
    while (!queue.empty()) {
        const TaskId taskId = queue.front();
        queue.pop_front();
        order.push_back(byId.at(taskId));
        for (const TaskId& dependentId : dependents[taskId]) {
            if (--indegree[dependentId] == 0) {
                queue.push_back(dependentId);
            }
        }
    }

    if (order.size() != tasks.size()) {
        reasons.push_back("Dependency graph contains a cycle");
        order.clear();
    }
    return order;
}

std::vector<const Task*> Scheduler::rankReadyTasks(
    std::span<const Task* const> tasks,
    TimePoint now
) const {
    std::vector<const Task*> ranked(tasks.begin(), tasks.end());
    std::stable_sort(
        ranked.begin(),
        ranked.end(),
        [&](const Task* first, const Task* second) {
            const double firstScore =
                priorityStrategy_->evaluate(*first, now).score;
            const double secondScore =
                priorityStrategy_->evaluate(*second, now).score;
            if (firstScore != secondScore) {
                return firstScore > secondScore;
            }
            if (first->deadline != second->deadline) {
                return first->deadline < second->deadline;
            }
            return first->id < second->id;
        }
    );
    return ranked;
}

std::vector<TimeSlot> Scheduler::findAvailableSlots(
    const Schedule& schedule,
    TimePoint from,
    TimePoint until
) const {
    std::vector<TimeSlot> available;
    if (until <= from) {
        return available;
    }

    const DailyStudyWindow& window = validator_->studyWindow();
    const auto firstLocalDay = std::chrono::floor<std::chrono::days>(
        from + window.utcOffset
    );
    const auto lastLocalDay = std::chrono::floor<std::chrono::days>(
        until + window.utcOffset
    );

    for (auto localDay = firstLocalDay;
         localDay <= lastLocalDay;
         localDay += std::chrono::days{1}) {
        const TimePoint dayStartUtc{
            localDay.time_since_epoch() - window.utcOffset
        };
        const TimePoint windowStart = std::max(
            from,
            dayStartUtc + window.start
        );
        const TimePoint windowEnd = std::min(
            until,
            dayStartUtc + window.end
        );
        if (windowEnd <= windowStart) {
            continue;
        }

        std::vector<TimeSlot> occupied;
        for (const FixedEvent& event : schedule.fixedEvents) {
            if (event.time.start < windowEnd
                && windowStart < event.time.end) {
                occupied.push_back({
                    std::max(event.time.start, windowStart),
                    std::min(event.time.end, windowEnd)
                });
            }
        }
        for (const StudySession& session : schedule.studySessions) {
            if (occupiesTime(session.status)
                && session.time.start < windowEnd
                && windowStart < session.time.end) {
                occupied.push_back({
                    std::max(session.time.start, windowStart),
                    std::min(session.time.end, windowEnd)
                });
            }
        }

        std::sort(
            occupied.begin(),
            occupied.end(),
            [](const TimeSlot& first, const TimeSlot& second) {
                return first.start < second.start;
            }
        );

        TimePoint cursor = windowStart;
        for (const TimeSlot& busy : occupied) {
            TimePoint candidateStart = alignUp(
                cursor,
                config_.slotGranularity
            );
            if (candidateStart < busy.start) {
                available.push_back({candidateStart, busy.start});
            }
            cursor = std::max(cursor, busy.end);
        }

        const TimePoint candidateStart = alignUp(
            cursor,
            config_.slotGranularity
        );
        if (candidateStart < windowEnd) {
            available.push_back({candidateStart, windowEnd});
        }
    }

    return available;
}

SchedulingResult Scheduler::makeFailure(
    SchedulingStatus status,
    const Schedule& unchangedSchedule,
    std::vector<std::string> reasons
) const {
    return {status, unchangedSchedule, std::move(reasons)};
}

} // namespace edu_schedule
