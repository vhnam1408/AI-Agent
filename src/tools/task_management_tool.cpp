#include "task_management_tool.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace edu_schedule {
namespace {

using json = nlohmann::json;

std::optional<TaskStatus> parseTaskStatus(std::string_view str) {
    if (str == "Todo" || str == "todo") return TaskStatus::Todo;
    if (str == "InProgress" || str == "in_progress") return TaskStatus::InProgress;
    if (str == "Done" || str == "done") return TaskStatus::Done;
    if (str == "Blocked" || str == "blocked") return TaskStatus::Blocked;
    if (str == "Cancelled" || str == "cancelled") return TaskStatus::Cancelled;
    return std::nullopt;
}

} // namespace

TaskManagementTool::TaskManagementTool(std::shared_ptr<ITaskRepository> repository)
    : repository_(std::move(repository)) {
    if (!repository_) {
        throw std::invalid_argument("TaskManagementTool requires a valid TaskRepository");
    }
}

std::string_view TaskManagementTool::name() const noexcept {
    return "task_manager";
}

std::string_view TaskManagementTool::description() const noexcept {
    return "Create, update status, modify remaining time, or delete tasks in TaskRepository.";
}

std::string_view TaskManagementTool::schema() const noexcept {
    return R"({
  "type": "object",
  "required": ["action"],
  "properties": {
    "action": {"type": "string", "enum": ["add", "update_status", "update_remaining", "delete"]},
    "task_id": {"type": "string"},
    "status": {"type": "string", "enum": ["Todo", "InProgress", "Done", "Blocked", "Cancelled"]},
    "remaining_minutes": {"type": "integer", "minimum": 0},
    "task": {
      "type": "object",
      "required": ["id", "title"],
      "properties": {
        "id": {"type": "string"},
        "title": {"type": "string"},
        "course_id": {"type": "string"},
        "estimated_minutes": {"type": "integer", "minimum": 0}
      }
    }
  }
})";
}

ToolResult TaskManagementTool::execute(std::string_view input) {
    try {
        const auto req = json::parse(input);
        if (!req.is_object()) {
            return {false, "Expected JSON object input for task_manager."};
        }

        const std::string action = req.value("action", "");
        if (action.empty()) {
            return {false, "Missing required 'action' field."};
        }

        if (action == "add") {
            if (!req.contains("task") || !req["task"].is_object()) {
                return {false, "Missing 'task' object for 'add' action."};
            }
            const auto& tJson = req["task"];
            Task task;
            task.id = tJson.value("id", "");
            task.title = tJson.value("title", "");
            task.courseId = tJson.value("course_id", "");
            task.estimatedMinutes = tJson.value("estimated_minutes", 0);
            task.remainingMinutes = tJson.value("remaining_minutes", task.estimatedMinutes);

            if (tJson.contains("dependencies") && tJson["dependencies"].is_array()) {
                for (const auto& dep : tJson["dependencies"]) {
                    if (dep.is_string()) {
                        task.dependencies.push_back(dep.get<std::string>());
                    }
                }
            }

            if (!task.isValid()) {
                return {false, "Task is invalid: id must not be empty and duration must be non-negative."};
            }

            if (!repository_->add(task)) {
                return {false, "Failed to add task: task already exists or invalid."};
            }
            return {true, "Task added successfully: " + task.id};
        }

        if (action == "update_status") {
            const std::string taskId = req.value("task_id", "");
            if (taskId.empty()) {
                return {false, "Missing 'task_id' for update_status."};
            }

            const std::string statusStr = req.value("status", "");
            const auto statusOpt = parseTaskStatus(statusStr);
            if (!statusOpt) {
                return {false, "Invalid status: " + statusStr};
            }

            auto taskOpt = repository_->findById(taskId);
            if (!taskOpt) {
                return {false, "Task not found: " + taskId};
            }

            taskOpt->status = *statusOpt;
            if (*statusOpt == TaskStatus::Done) {
                taskOpt->remainingMinutes = 0;
            }

            if (!repository_->update(*taskOpt)) {
                return {false, "Failed to update task: " + taskId};
            }
            return {true, "Updated task " + taskId + " status to " + statusStr};
        }

        if (action == "update_remaining") {
            const std::string taskId = req.value("task_id", "");
            if (taskId.empty()) {
                return {false, "Missing 'task_id' for update_remaining."};
            }

            if (!req.contains("remaining_minutes") || !req["remaining_minutes"].is_number_integer()) {
                return {false, "Missing or invalid 'remaining_minutes'."};
            }

            const int remaining = req["remaining_minutes"].get<int>();
            if (remaining < 0) {
                return {false, "remaining_minutes cannot be negative."};
            }

            auto taskOpt = repository_->findById(taskId);
            if (!taskOpt) {
                return {false, "Task not found: " + taskId};
            }

            taskOpt->remainingMinutes = remaining;
            if (remaining == 0 && taskOpt->status != TaskStatus::Done) {
                taskOpt->status = TaskStatus::Done;
            }

            if (!repository_->update(*taskOpt)) {
                return {false, "Failed to update task: " + taskId};
            }
            return {true, "Updated task " + taskId + " remaining minutes to " + std::to_string(remaining)};
        }

        if (action == "delete" || action == "remove") {
            const std::string taskId = req.value("task_id", "");
            if (taskId.empty()) {
                return {false, "Missing 'task_id' for delete."};
            }

            if (!repository_->remove(taskId)) {
                return {false, "Failed to delete task: not found with id " + taskId};
            }
            return {true, "Task deleted successfully: " + taskId};
        }

        return {false, "Unknown action: " + action};
    } catch (const json::exception& e) {
        return {false, std::string("JSON parsing error in task_manager: ") + e.what()};
    } catch (const std::exception& e) {
        return {false, std::string("Task management error: ") + e.what()};
    }
}

} // namespace edu_schedule
