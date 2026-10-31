#include "dependency_dag.h"

#include <stdexcept>

namespace edu_schedule {

bool DependencyDAG::addTask(std::string_view taskId) {
    if (taskId.empty()) return false;
    return edges_.try_emplace(std::string(taskId)).second;
}

bool DependencyDAG::addDependency(std::string_view prerequisiteId,
                                  std::string_view dependentId) {
    if (prerequisiteId.empty() || dependentId.empty()
        || prerequisiteId == dependentId
        || !edges_.contains(std::string(prerequisiteId))
        || !edges_.contains(std::string(dependentId))) {
        return false;
    }

    auto& dependents = edges_.at(std::string(prerequisiteId));
    const std::string dependent(dependentId);
    for (const auto& existing : dependents) {
        if (existing == dependent) return false;
    }
    dependents.push_back(dependent);
    return true;
}

bool DependencyDAG::containsTask(std::string_view taskId) const {
    return edges_.contains(std::string(taskId));
}

const std::vector<std::string>& DependencyDAG::dependentsOf(
    std::string_view prerequisiteId) const {
    static const std::vector<std::string> empty;
    const auto it = edges_.find(std::string(prerequisiteId));
    return it == edges_.end() ? empty : it->second;
}

DependencyDAG DependencyDAG::fromTasks(const std::vector<Task>& tasks) {
    DependencyDAG dag;
    for (const auto& task : tasks) {
        if (!dag.addTask(task.id)) {
            throw std::invalid_argument("Task IDs must be non-empty and unique.");
        }
    }

    for (const auto& task : tasks) {
        for (const auto& prerequisite : task.dependencies) {
            if (!dag.addDependency(prerequisite, task.id)) {
                throw std::invalid_argument(
                    "Invalid or duplicate dependency: " + prerequisite + " -> " + task.id);
            }
        }
    }
    return dag;
}

} // namespace edu_schedule
