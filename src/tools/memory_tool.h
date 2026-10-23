#pragma once

#include "../agent/Tool.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

// ============================================================================
// MemoryEntry: Bản ghi ký ức lưu trữ trong bảng `memories` của SQLite
// ============================================================================
struct MemoryEntry {
    std::string memory_id;
    std::string content;
    std::string source_type;
    std::string source_id;
    std::string created_at_utc;
};

// ============================================================================
// MemoryTool (Core Tool 2): Thiết lập SQLite schema (TASK-04) và thực hiện
// lưu trữ / truy vấn ký ức dài hạn (memory_save / memory_search).
// ============================================================================
class MemoryTool : public ITool {
public:
    enum class Mode {
        Unified,    // Hỗ trợ cả memory_save và memory_search qua JSON / prefix
        SaveOnly,   // Chuyên biệt cho tool "memory_save"
        SearchOnly  // Chuyên biệt cho tool "memory_search"
    };

    explicit MemoryTool(
        std::filesystem::path dbPath = "eduschedule.db",
        Mode mode = Mode::Unified
    );

    ~MemoryTool() override = default;

    [[nodiscard]]
    std::string_view name() const noexcept override;

    [[nodiscard]]
    std::string_view description() const noexcept override;

    [[nodiscard]]
    ToolResult execute(std::string_view input) override;

    // Khởi tạo toàn bộ bảng và chỉ mục SQLite theo schema từ TASK-04
    [[nodiscard]]
    bool initializeSchema(std::string* errorMessage = nullptr);

    // Kiểm tra một bảng đã tồn tại trong file SQLite hay chưa
    [[nodiscard]]
    bool hasTable(std::string_view tableName) const;

    // Lưu trực tiếp một bản ghi ký ức vào bảng `memories`
    [[nodiscard]]
    ToolResult saveMemory(
        std::string_view content,
        std::string_view memoryId = "",
        std::string_view sourceType = "note",
        std::string_view sourceId = ""
    );

    // Truy vấn ký ức theo từ khóa từ bảng `memories` (trả về ToolResult chứa JSON)
    [[nodiscard]]
    ToolResult searchMemory(
        std::string_view query,
        int limit = 10,
        std::string_view sourceType = "",
        std::string_view sourceId = ""
    ) const;

    // Truy vấn danh sách MemoryEntry phục vụ tầng C++ và kiểm thử
    [[nodiscard]]
    std::vector<MemoryEntry> queryMemories(
        std::string_view query,
        int limit = 10,
        std::string_view sourceType = "",
        std::string_view sourceId = ""
    ) const;

    [[nodiscard]]
    const std::filesystem::path& getDbPath() const noexcept {
        return dbPath_;
    }

private:
    std::filesystem::path dbPath_;
    Mode mode_ = Mode::Unified;
    bool schemaInitialized_ = false;
    std::string initError_;
};

// ============================================================================
// MemorySaveTool: Wrapper đăng ký vào ToolRegistry dưới tên "memory_save"
// ============================================================================
class MemorySaveTool final : public MemoryTool {
public:
    explicit MemorySaveTool(std::filesystem::path dbPath = "eduschedule.db")
        : MemoryTool(std::move(dbPath), Mode::SaveOnly) {}
};

// ============================================================================
// MemorySearchTool: Wrapper đăng ký vào ToolRegistry dưới tên "memory_search"
// ============================================================================
class MemorySearchTool final : public MemoryTool {
public:
    explicit MemorySearchTool(std::filesystem::path dbPath = "eduschedule.db")
        : MemoryTool(std::move(dbPath), Mode::SearchOnly) {}
};

} // namespace edu_schedule

