#include "Task_domain.h"

#include <utility>

namespace edu_schedule {

bool TaskDomain::addTask(std::unique_ptr<Task> task) {
    if (!task || !task->isValid()) {
        return false;
    }
    const TaskId id = task->id;
    if (tasks_.contains(id)) {
        return false;
    }
    tasks_.emplace(id, std::move(task));
    return true;
}

bool TaskDomain::addCourse(std::unique_ptr<Course> course) {
    if (!course || !course->isValid()) {
        return false;
    }
    const std::string id = course->id;
    if (courses_.contains(id)) {
        return false;
    }
    courses_.emplace(id, std::move(course));
    return true;
}

Task* TaskDomain::findTask(std::string_view id) {
    const auto it = tasks_.find(std::string(id));
    return it == tasks_.end() ? nullptr : it->second.get();
}

const Task* TaskDomain::findTask(std::string_view id) const {
    const auto it = tasks_.find(std::string(id));
    return it == tasks_.end() ? nullptr : it->second.get();
}

Course* TaskDomain::findCourse(std::string_view id) {
    const auto it = courses_.find(std::string(id));
    return it == courses_.end() ? nullptr : it->second.get();
}

const Course* TaskDomain::findCourse(std::string_view id) const {
    const auto it = courses_.find(std::string(id));
    return it == courses_.end() ? nullptr : it->second.get();
}

bool TaskDomain::removeTask(std::string_view id) {
    return tasks_.erase(std::string(id)) > 0;
}

bool TaskDomain::removeCourse(std::string_view id) {
    return courses_.erase(std::string(id)) > 0;
}

std::size_t TaskDomain::taskCount() const noexcept { return tasks_.size(); }
std::size_t TaskDomain::courseCount() const noexcept { return courses_.size(); }

} // namespace edu_schedule
