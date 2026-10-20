#include "TaskLookupTool.h"

namespace edu_schedule {

TaskLookupTool::TaskLookupTool(const Task& task)
    : task_(task) {
}

std::string_view TaskLookupTool::name() const noexcept {
    return "task_lookup";
}

std::string_view TaskLookupTool::description() const noexcept {
    return "Find a task by ID";
}

ToolResult TaskLookupTool::execute(std::string_view input) {
    if (input == task_.id) {
        return {true, "Found task: " + task_.title};
    }

    return {false, "Task not found"};
}

} // namespace edu_schedule
