#include "TaskLookupTool.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace edu_schedule {
namespace {

using json = nlohmann::json;

std::string taskStatusToString(TaskStatus status) {
    switch (status) {
    case TaskStatus::Todo: return "Todo";
    case TaskStatus::InProgress: return "InProgress";
    case TaskStatus::Done: return "Done";
    case TaskStatus::Blocked: return "Blocked";
    case TaskStatus::Cancelled: return "Cancelled";
    }
    return "Todo";
}

std::optional<TaskStatus> parseTaskStatus(std::string_view str) {
    if (str == "Todo" || str == "todo") return TaskStatus::Todo;
    if (str == "InProgress" || str == "in_progress") return TaskStatus::InProgress;
    if (str == "Done" || str == "done") return TaskStatus::Done;
    if (str == "Blocked" || str == "blocked") return TaskStatus::Blocked;
    if (str == "Cancelled" || str == "cancelled") return TaskStatus::Cancelled;
    return std::nullopt;
}

json serializeTask(const Task& task) {
    json j;
    j["id"] = task.id;
    j["title"] = task.title;
    j["course_id"] = task.courseId;
    j["estimated_minutes"] = task.estimatedMinutes;
    j["remaining_minutes"] = task.remainingMinutes;
    j["status"] = taskStatusToString(task.status);
    j["dependencies"] = task.dependencies;
    return j;
}

} // namespace

TaskLookupTool::TaskLookupTool(const Task& task)
    : fallbackTask_(&task) {}

TaskLookupTool::TaskLookupTool(std::shared_ptr<const ITaskRepository> repository)
    : repository_(std::move(repository)) {}

std::string_view TaskLookupTool::name() const noexcept {
    return "task_lookup";
}

std::string_view TaskLookupTool::description() const noexcept {
    return "Find tasks by ID, course, or status from TaskRepository, or retrieve task details.";
}

std::string_view TaskLookupTool::schema() const noexcept {
    return R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["find_by_id", "find_by_course", "find_by_status", "list_all", "count"]},
    "task_id": {"type": "string"},
    "course_id": {"type": "string"},
    "status": {"type": "string", "enum": ["Todo", "InProgress", "Done", "Blocked", "Cancelled"]}
  }
})";
}

ToolResult TaskLookupTool::execute(std::string_view input) {
    // 1. Thử phân tích cú pháp JSON
    if (!input.empty() && input.front() == '{') {
        try {
            const auto req = json::parse(input);
            if (!repository_) {
                // Fallback nếu không có repository nhưng có fallbackTask_
                if (fallbackTask_) {
                    const std::string requestedId = req.value("task_id", req.value("id", ""));
                    if (requestedId == fallbackTask_->id) {
                        return {true, serializeTask(*fallbackTask_).dump(2)};
                    }
                    return {false, "Task not found"};
                }
                return {false, "No repository connected to TaskLookupTool."};
            }

            const std::string action = req.value("action", "find_by_id");

            if (action == "find_by_id" || action == "lookup") {
                const std::string id = req.value("task_id", req.value("id", ""));
                if (id.empty()) {
                    return {false, "Missing task_id."};
                }
                const auto taskOpt = repository_->findById(id);
                if (!taskOpt.has_value()) {
                    return {false, "Task not found with ID: " + id};
                }
                return {true, serializeTask(*taskOpt).dump(2)};
            }

            if (action == "find_by_course") {
                const std::string courseId = req.value("course_id", "");
                const auto tasks = repository_->findByCourse(courseId);
                json arr = json::array();
                for (const auto& t : tasks) {
                    arr.push_back(serializeTask(t));
                }
                return {true, arr.dump(2)};
            }

            if (action == "find_by_status") {
                const std::string statusStr = req.value("status", "Todo");
                const auto statusOpt = parseTaskStatus(statusStr);
                if (!statusOpt) {
                    return {false, "Invalid status: " + statusStr};
                }
                const auto tasks = repository_->findByStatus(*statusOpt);
                json arr = json::array();
                for (const auto& t : tasks) {
                    arr.push_back(serializeTask(t));
                }
                return {true, arr.dump(2)};
            }

            if (action == "list_all" || action == "find_all") {
                const auto tasks = repository_->findAll();
                json arr = json::array();
                for (const auto& t : tasks) {
                    arr.push_back(serializeTask(t));
                }
                return {true, arr.dump(2)};
            }

            if (action == "count") {
                return {true, std::to_string(repository_->count())};
            }

            return {false, "Unknown action: " + action};
        } catch (const json::exception& e) {
            return {false, std::string("JSON parsing error: ") + e.what()};
        }
    }

    // 2. Xử lý đầu vào chuỗi ID thuần (tương thích ngược hoàn toàn)
    if (repository_) {
        const auto taskOpt = repository_->findById(input);
        if (taskOpt.has_value()) {
            return {true, "Found task: " + taskOpt->title};
        }
        return {false, "Task not found"};
    }

    if (fallbackTask_) {
        if (input == fallbackTask_->id) {
            return {true, "Found task: " + fallbackTask_->title};
        }
        return {false, "Task not found"};
    }

    return {false, "No task or repository available for lookup."};
}

} // namespace edu_schedule
