#pragma once

#include "Task.h"
#include "domain_types.h"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace edu_schedule {

// ============================================================================
// ITaskRepository: Interface trừu tượng đại diện cho Repository Pattern của Task
// ----------------------------------------------------------------------------
// QUY ƯỚC BẮT BUỘC:
// - Cung cấp đầy đủ các thao tác CRUD cơ bản cho thực thể Task.
// - Tách biệt logic nghiệp vụ khỏi cách thức lưu trữ dữ liệu (Database / Memory).
// - Các phương thức tra cứu trả về bản sao hoặc std::optional để đảm bảo an toàn,
//   không làm rò rỉ con trỏ nội bộ có thể bị hủy trong môi trường đa luồng.
// ============================================================================
class ITaskRepository {
public:
    virtual ~ITaskRepository() = default;

    // Thêm một nhiệm vụ mới vào kho lưu trữ
    // Trả về true nếu thêm thành công, false nếu task không hợp lệ hoặc đã trùng ID
    virtual bool add(const Task& task) = 0;

    // Tìm kiếm nhiệm vụ theo mã định danh duy nhất (ID)
    // Trả về std::optional chứa bản sao Task nếu tìm thấy, hoặc std::nullopt nếu không tồn tại
    [[nodiscard]] virtual std::optional<Task> findById(std::string_view id) const = 0;

    // Lấy toàn bộ danh sách nhiệm vụ hiện có trong kho lưu trữ
    [[nodiscard]] virtual std::vector<Task> findAll() const = 0;

    // Lọc danh sách nhiệm vụ thuộc một môn học cụ thể
    [[nodiscard]] virtual std::vector<Task> findByCourse(std::string_view courseId) const = 0;

    // Lọc danh sách nhiệm vụ theo trạng thái thực hiện
    [[nodiscard]] virtual std::vector<Task> findByStatus(TaskStatus status) const = 0;

    // Cập nhật thông tin của một nhiệm vụ đã tồn tại
    // Trả về true nếu cập nhật thành công, false nếu không tìm thấy ID tương ứng
    virtual bool update(const Task& task) = 0;

    // Xóa nhiệm vụ khỏi kho lưu trữ theo ID
    // Trả về true nếu xóa thành công, false nếu không tìm thấy ID
    virtual bool remove(std::string_view id) = 0;

    // Kiểm tra sự tồn tại của một nhiệm vụ theo ID
    [[nodiscard]] virtual bool exists(std::string_view id) const noexcept = 0;

    // Trả về tổng số lượng nhiệm vụ đang lưu trữ
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;

    // Xóa sạch toàn bộ dữ liệu nhiệm vụ (hữu ích cho việc làm mới hoặc kiểm thử)
    virtual void clear() noexcept = 0;
};

// ============================================================================
// InMemoryTaskRepository: Bản cài đặt đầu tiên lưu trữ trong bộ nhớ RAM (TASK-14)
// ----------------------------------------------------------------------------
// - Sử dụng cấu trúc bảng băm std::unordered_map để đạt độ phức tạp tìm kiếm O(1).
// - Đảm bảo tính đóng gói và an toàn dữ liệu cho các luồng xử lý của MVP.
// - Sẵn sàng để mở rộng hoặc thay thế bằng SQLiteTaskRepository ở TASK-20.
// ============================================================================
class InMemoryTaskRepository : public ITaskRepository {
public:
    InMemoryTaskRepository() = default;
    ~InMemoryTaskRepository() override = default;

    // Cấm sao chép repository để tránh nhân bản dữ liệu ngoài ý muốn
    InMemoryTaskRepository(const InMemoryTaskRepository&) = delete;
    InMemoryTaskRepository& operator=(const InMemoryTaskRepository&) = delete;

    // Cho phép di chuyển quyền sở hữu
    InMemoryTaskRepository(InMemoryTaskRepository&&) noexcept = default;
    InMemoryTaskRepository& operator=(InMemoryTaskRepository&&) noexcept = default;

    bool add(const Task& task) override;
    [[nodiscard]] std::optional<Task> findById(std::string_view id) const override;
    [[nodiscard]] std::vector<Task> findAll() const override;
    [[nodiscard]] std::vector<Task> findByCourse(std::string_view courseId) const override;
    [[nodiscard]] std::vector<Task> findByStatus(TaskStatus status) const override;
    bool update(const Task& task) override;
    bool remove(std::string_view id) override;
    [[nodiscard]] bool exists(std::string_view id) const noexcept override;
    [[nodiscard]] std::size_t count() const noexcept override;
    void clear() noexcept override;

private:
    std::unordered_map<TaskId, Task> tasks_;
};

// Định danh thuận tiện cho bản kho lưu trữ mặc định
using TaskRepository = InMemoryTaskRepository;

} // namespace edu_schedule
