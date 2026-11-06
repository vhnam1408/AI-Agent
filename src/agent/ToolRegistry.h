#pragma once

#include "Tool.h"
#include "../models/TaskRepository.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace edu_schedule {

// Callback hook theo dõi thực thi công cụ (dành cho Logging, Harness và Trajectory)
using ToolExecutionHook = std::function<void(
    std::string_view toolName,
    std::string_view input,
    const ToolResult& result,
    long long durationMs)>;

// ============================================================================
// ToolRegistry: Quản lý vòng đời, phân quyền và điều phối thực thi Tool (TASK-26)
//
// QUY ƯỚC & NGUYÊN TẮC:
// - Registry sở hữu các công cụ thông qua std::unique_ptr<ITool>.
// - Con trỏ trả về từ findTool() là con trỏ mượn, không được sống lâu hơn Registry.
// - Nguyên tắc an toàn Default-Deny: mọi công cụ bị từ chối cho đến khi allowTool().
// - Chuẩn hóa xử lý ngoại lệ: execute() bắt mọi exception từ Tool và bọc vào ToolResult.
// - Tích hợp TaskRepository: Cho phép đồng bộ tự động hoặc chia sẻ kho dữ liệu cho các Tool.
// ============================================================================
class ToolRegistry {
public:
    ToolRegistry() = default;
    ~ToolRegistry() = default;
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;
    ToolRegistry(ToolRegistry&&) noexcept = default;
    ToolRegistry& operator=(ToolRegistry&&) noexcept = default;

    // Đăng ký công cụ qua giao diện trừu tượng
    void registerTool(std::unique_ptr<ITool> tool);

    // Đăng ký tiện ích với kiểu cụ thể thỏa mãn concept ToolType
    template <ToolType T>
    void registerTool(std::unique_ptr<T> tool) {
        registerTool(std::unique_ptr<ITool>(std::move(tool)));
    }

    [[nodiscard]] ITool* findTool(std::string_view name);
    [[nodiscard]] const ITool* findTool(std::string_view name) const;
    [[nodiscard]] bool hasTool(std::string_view name) const;

    // Chính sách bảo mật (Security Policy)
    void allowTool(std::string_view name);
    void denyTool(std::string_view name);
    void allowAllTools();
    void denyAllTools();
    void clearToolPolicy() noexcept;
    [[nodiscard]] bool isToolAllowed(std::string_view name) const;

    // Thực thi công cụ đã chuẩn hóa an toàn (bắt ngoại lệ và kích hoạt hook)
    [[nodiscard]] ToolResult execute(std::string_view name, std::string_view input);

    // Danh sách và metadata
    [[nodiscard]] std::vector<std::string> listTools() const;
    [[nodiscard]] std::vector<std::string> listAllowedTools() const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::string exportToolsSchemaJson() const;

    // Quản lý kết nối với TaskRepository (TASK-26)
    void setTaskRepository(std::shared_ptr<ITaskRepository> repository);
    [[nodiscard]] std::shared_ptr<ITaskRepository> getTaskRepository() const noexcept;
    [[nodiscard]] bool hasTaskRepository() const noexcept;

    // Tự động đồng bộ kết quả Tool (như task_decomposer) vào TaskRepository
    void setAutoSyncResults(bool enable) noexcept;
    [[nodiscard]] bool isAutoSyncResults() const noexcept;

    // Thiết lập Hook theo dõi thực thi
    void setExecutionHook(ToolExecutionHook hook);

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>> tools_;
    std::unordered_set<std::string> allowedTools_;
    std::unordered_set<std::string> deniedTools_;

    std::shared_ptr<ITaskRepository> taskRepository_;
    bool autoSyncResults_ = false;
    ToolExecutionHook executionHook_;

    void syncToolResultToRepository(std::string_view outputJson);
};

} // namespace edu_schedule
