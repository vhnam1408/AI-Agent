#pragma once

#include "../agent/Tool.h"
#include "../models/Task.h"
#include "../models/TaskRepository.h"

#include <memory>
#include <optional>
#include <string_view>

namespace edu_schedule {

// ============================================================================
// TaskLookupTool: Công cụ tra cứu thông tin Task từ TaskRepository (TASK-26)
//
// Khả năng hỗ trợ:
// 1. Tương thích ngược: Khởi tạo với một Task cụ thể (const Task&).
// 2. Tích hợp Repository: Khởi tạo với std::shared_ptr<const ITaskRepository>.
// 3. Xử lý đầu vào:
//    - Chuỗi ID đơn giản (vd: "T1").
//    - JSON object chứa action:
//      - {"action": "find_by_id", "task_id": "..."}
//      - {"action": "find_by_course", "course_id": "..."}
//      - {"action": "find_by_status", "status": "Todo|InProgress|Done"}
//      - {"action": "list_all"}
//      - {"action": "count"}
// ============================================================================
class TaskLookupTool final : public ITool {
public:
    // Constructor tương thích ngược cho Task đơn lẻ
    explicit TaskLookupTool(const Task& task);
    TaskLookupTool(Task&&) = delete;
    TaskLookupTool(const Task&&) = delete;

    // Constructor tích hợp với TaskRepository
    explicit TaskLookupTool(std::shared_ptr<const ITaskRepository> repository);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] std::string_view schema() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;

private:
    const Task* fallbackTask_ = nullptr;
    std::shared_ptr<const ITaskRepository> repository_;
};

} // namespace edu_schedule
