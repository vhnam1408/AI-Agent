#pragma once

#include "../models/Task.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace edu_schedule {

// Dependency direction is prerequisite -> dependent. Cycle detection and
// topological ordering are intentionally left for TASK-19.
class DependencyDAG {
public:
    [[nodiscard]] bool addTask(std::string_view taskId);
    // Returns false for missing IDs, self-dependency, or a duplicate edge.
    [[nodiscard]] bool addDependency(std::string_view prerequisiteId,
                                     std::string_view dependentId);

    [[nodiscard]] bool containsTask(std::string_view taskId) const;
    [[nodiscard]] const std::vector<std::string>& dependentsOf(
        std::string_view prerequisiteId) const;

    // Imports Task::dependencies where each listed ID is a prerequisite.
    [[nodiscard]] static DependencyDAG fromTasks(const std::vector<Task>& tasks);

private:
    std::unordered_map<std::string, std::vector<std::string>> edges_;
};

} // namespace edu_schedule
