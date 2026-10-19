
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>

#include "TaskStatus.h"
#include "Priority.h"

using TimePoint = std::chrono::system_clock::time_point;

struct Task {
    std::string id;
    std::string title;
    std::string courseId;

    std::optional<TimePoint> deadline;

    int estimatedMinutes = 0;
    int remainingMinutes = 0;

    Priority priority = Priority::Mid;
    TaskStatus status = TaskStatus::Todo;

    std::vector<std::string> dependencies;
};
