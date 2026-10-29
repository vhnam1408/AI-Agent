#include "TaskRepository.h"

namespace edu_schedule {

bool InMemoryTaskRepository::add(const Task& task) {
    // Kiểm tra tính hợp lệ của Task: ID không được rỗng và thời lượng phải hợp lệ
    if (!task.isValid()) {
        return false;
    }

    // Không cho phép thêm nếu ID nhiệm vụ đã tồn tại trong kho lưu trữ
    const std::string key(task.id);
    if (tasks_.contains(key)) {
        return false;
    }

    tasks_.emplace(key, task);
    return true;
}

std::optional<Task> InMemoryTaskRepository::findById(std::string_view id) const {
    const auto it = tasks_.find(std::string(id));
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<Task> InMemoryTaskRepository::findAll() const {
    std::vector<Task> result;
    result.reserve(tasks_.size());
    for (const auto& [id, task] : tasks_) {
        result.push_back(task);
    }
    return result;
}

std::vector<Task> InMemoryTaskRepository::findByCourse(std::string_view courseId) const {
    std::vector<Task> result;
    for (const auto& [id, task] : tasks_) {
        if (task.courseId == courseId) {
            result.push_back(task);
        }
    }
    return result;
}

std::vector<Task> InMemoryTaskRepository::findByStatus(TaskStatus status) const {
    std::vector<Task> result;
    for (const auto& [id, task] : tasks_) {
        if (task.status == status) {
            result.push_back(task);
        }
    }
    return result;
}

bool InMemoryTaskRepository::update(const Task& task) {
    // Kiểm tra tính hợp lệ của thông tin cập nhật
    if (!task.isValid()) {
        return false;
    }

    const auto it = tasks_.find(std::string(task.id));
    if (it == tasks_.end()) {
        return false;
    }

    it->second = task;
    return true;
}

bool InMemoryTaskRepository::remove(std::string_view id) {
    return tasks_.erase(std::string(id)) > 0;
}

bool InMemoryTaskRepository::exists(std::string_view id) const noexcept {
    return tasks_.contains(std::string(id));
}

std::size_t InMemoryTaskRepository::count() const noexcept {
    return tasks_.size();
}

void InMemoryTaskRepository::clear() noexcept {
    tasks_.clear();
}

} // namespace edu_schedule
