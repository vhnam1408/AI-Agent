
#include "TaskLookupTool.h"

TaskLookupTool::TaskLookupTool(const Task& task)
    : task(task) {
}

std::string TaskLookupTool::name() const {
    return "task_lookup";
}

std::string TaskLookupTool::description() const {
    return "Find a task by ID";
}

std::string TaskLookupTool::execute(const std::string& input) {
    if (input == task.id) {
        return "Found task: " + task.title;
    }

    return "Task not found";
}