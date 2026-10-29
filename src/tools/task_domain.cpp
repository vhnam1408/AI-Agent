#include "Task_domain.h"

#include <utility>

namespace edu_schedule {

// Thêm một tác vụ (Task) mới vào hệ thống quản lý TaskDomain.
// Nhận vào một std::unique_ptr<Task> để chuyển giao toàn bộ quyền sở hữu (ownership) đối tượng.
// Trả về true nếu thêm thành công, hoặc false nếu dữ liệu không hợp lệ hay ID đã tồn tại.
bool TaskDomain::addTask(std::unique_ptr<Task> task) {
    // 1. Kiểm tra con trỏ task không rỗng và nội dung dữ liệu phải hợp lệ theo nghiệp vụ.
    if (!task || !task->isValid()) {
        return false;
    }

    // 2. Kiểm tra xem mã định danh (id) của task đã tồn tại trong danh sách chưa (tránh trùng lặp).
    const TaskId id = task->id;
    if (tasks_.contains(id)) {
        return false;
    }

    // 3. Chuyển giao quyền sở hữu (move ownership) của task vào bảng băm quản lý tasks_.
    tasks_.emplace(id, std::move(task));
    return true;
}

// Thêm một khóa học (Course) mới vào hệ thống quản lý.
// Nhận vào std::unique_ptr<Course> để chuyển quyền sở hữu vào bảng băm courses_.
// Trả về true nếu thêm thành công, false nếu dữ liệu không hợp lệ hoặc mã khóa học đã tồn tại.
bool TaskDomain::addCourse(std::unique_ptr<Course> course) {
    // 1. Kiểm tra con trỏ course không rỗng và thông tin khóa học phải hợp lệ.
    if (!course || !course->isValid()) {
        return false;
    }

    // 2. Kiểm tra mã khóa học đã tồn tại trong danh sách chưa để tránh trùng lặp.
    const std::string id = course->id;
    if (courses_.contains(id)) {
        return false;
    }

    // 3. Chuyển giao quyền sở hữu (move ownership) vào bảng băm courses_.
    courses_.emplace(id, std::move(course));
    return true;
}

// Tìm kiếm tác vụ theo mã định danh id (phiên bản non-const, cho phép chỉnh sửa).
// Trả về con trỏ thô (raw pointer) trỏ tới Task nếu tìm thấy, hoặc nullptr nếu không tồn tại.
// Lưu ý: Con trỏ trả về là con trỏ mượn (borrowed), bên gọi không được tự ý giải phóng bộ nhớ.
Task* TaskDomain::findTask(std::string_view id) {
    const auto it = tasks_.find(std::string(id));
    return it == tasks_.end() ? nullptr : it->second.get();
}

// Tìm kiếm tác vụ theo mã định danh id (phiên bản const, chỉ đọc).
// Trả về con trỏ const trỏ tới Task nếu tìm thấy, hoặc nullptr nếu không tồn tại.
const Task* TaskDomain::findTask(std::string_view id) const {
    const auto it = tasks_.find(std::string(id));
    return it == tasks_.end() ? nullptr : it->second.get();
}

// Tìm kiếm khóa học theo mã định danh id (phiên bản non-const, cho phép chỉnh sửa).
// Trả về con trỏ thô trỏ tới Course nếu tìm thấy, hoặc nullptr nếu không tồn tại.
Course* TaskDomain::findCourse(std::string_view id) {
    const auto it = courses_.find(std::string(id));
    return it == courses_.end() ? nullptr : it->second.get();
}

// Tìm kiếm khóa học theo mã định danh id (phiên bản const, chỉ đọc).
// Trả về con trỏ const trỏ tới Course nếu tìm thấy, hoặc nullptr nếu không tồn tại.
const Course* TaskDomain::findCourse(std::string_view id) const {
    const auto it = courses_.find(std::string(id));
    return it == courses_.end() ? nullptr : it->second.get();
}

// Xóa một tác vụ khỏi hệ thống dựa vào mã định danh id.
// Bộ nhớ của đối tượng Task sẽ được tự động giải phóng thông qua std::unique_ptr.
// Trả về true nếu tìm thấy và xóa thành công, false nếu không tìm thấy tác vụ mang id này.
bool TaskDomain::removeTask(std::string_view id) {
    return tasks_.erase(std::string(id)) > 0;
}

// Xóa một khóa học khỏi hệ thống dựa vào mã định danh id.
// Bộ nhớ của đối tượng Course sẽ được tự động giải phóng thông qua std::unique_ptr.
// Trả về true nếu xóa thành công, false nếu không tìm thấy khóa học mang id này.
bool TaskDomain::removeCourse(std::string_view id) {
    return courses_.erase(std::string(id)) > 0;
}

// Đếm tổng số lượng tác vụ (Task) hiện đang được quản lý trong hệ thống.
std::size_t TaskDomain::taskCount() const noexcept {
    return tasks_.size();
}

// Đếm tổng số lượng khóa học (Course) hiện đang được quản lý trong hệ thống.
std::size_t TaskDomain::courseCount() const noexcept {
    return courses_.size();
}

} // namespace edu_schedule
