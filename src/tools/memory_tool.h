#pragma once

#include "../agent/Tool.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace edu_schedule {

// ============================================================================
// MemoryToolBase: Lớp cơ sở quản lý kết nối và khởi tạo bảng SQLite (TASK-12)
// ----------------------------------------------------------------------------
// QUY ƯỚC BẮT BUỘC (theo tài liệu TASK-04):
// - Bật PRAGMA foreign_keys = ON trên mỗi kết nối SQLite.
// - Tự động khởi tạo các bảng chuẩn (courses, tasks, task_dependencies,
//   memories, memory_embeddings) khi khởi tạo công cụ.
// - Dùng Prepared Statements (sqlite3_prepare_v2) để chống SQL Injection.
// ============================================================================
class MemoryToolBase {
public:
    explicit MemoryToolBase(std::filesystem::path dbPath = "eduschedule.db");
    virtual ~MemoryToolBase() = default;

    // Khởi tạo toàn bộ bảng SQLite theo schema đã thiết kế ở TASK-04
    [[nodiscard]] bool initializeDatabase(std::string* errorMessage = nullptr);

    // Kiểm tra một bảng đã tồn tại trong file SQLite hay chưa (phục vụ kiểm thử)
    [[nodiscard]] bool hasTable(std::string_view tableName) const;

protected:
    std::filesystem::path dbPath_;
};

// ============================================================================
// MemorySaveTool: Công cụ lưu ghi chú / ký ức học tập vào SQLite
// ----------------------------------------------------------------------------
// Tên đăng ký trong ToolRegistry: "memory_save"
// Đầu vào (input):
//   - Văn bản thuần: "Hạn nộp bài tập OOP là ngày 30/10"
//   - Hoặc JSON: {"content": "...", "memory_id": "...", "source_type": "...", "source_id": "..."}
// ============================================================================
class MemorySaveTool final : public ITool, public MemoryToolBase {
public:
    explicit MemorySaveTool(std::filesystem::path dbPath = "eduschedule.db");

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;
};

// ============================================================================
// MemorySearchTool: Công cụ tìm kiếm ký ức học tập từ SQLite theo từ khóa
// ----------------------------------------------------------------------------
// Tên đăng ký trong ToolRegistry: "memory_search"
// Đầu vào (input):
//   - Từ khóa thuần: "OOP"
//   - Hoặc JSON: {"query": "OOP", "limit": 5}
// ============================================================================
class MemorySearchTool final : public ITool, public MemoryToolBase {
public:
    explicit MemorySearchTool(std::filesystem::path dbPath = "eduschedule.db");

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;
};

} // namespace edu_schedule
