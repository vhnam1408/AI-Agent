#pragma once

#include "TaskRepository.h"

#include <cstddef>
#include <filesystem>
#include <memory>

struct sqlite3;

namespace edu_schedule {

// ============================================================================
// SqliteTaskRepository: Kho lưu trữ Task bền vững trên SQLite (TASK-20)
//
// Cài đặt đầy đủ hợp đồng ITaskRepository để thay thế tương thích (Liskov) cho
// InMemoryTaskRepository mà không cần sửa đổi tầng nghiệp vụ hay các Tool khác:
// - Quản lý vòng đời kết nối sqlite3* và sqlite3_stmt* tự động bằng RAII.
// - Sử dụng 100% Prepared Statements để chống SQL Injection tuyệt đối.
// - Đảm bảo toàn vẹn giao dịch (Transaction) và khóa ngoại (Foreign Keys).
// ============================================================================
class SqliteTaskRepository final : public ITaskRepository {
public:
    explicit SqliteTaskRepository(std::filesystem::path dbPath = "eduschedule.db");
    ~SqliteTaskRepository() override;

    SqliteTaskRepository(const SqliteTaskRepository&) = delete;
    SqliteTaskRepository& operator=(const SqliteTaskRepository&) = delete;

    SqliteTaskRepository(SqliteTaskRepository&&) noexcept;
    SqliteTaskRepository& operator=(SqliteTaskRepository&&) noexcept;

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
    struct DbDeleter {
        void operator()(sqlite3* db) const noexcept;
    };

    std::filesystem::path dbPath_;
    std::unique_ptr<sqlite3, DbDeleter> db_;
};

// Bí danh tương thích với tên gọi trong tài liệu thiết kế TaskRepository.h
using SQLiteTaskRepository = SqliteTaskRepository;

} // namespace edu_schedule
