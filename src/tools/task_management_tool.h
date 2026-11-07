#pragma once

#include "../agent/Tool.h"
#include "../models/Task.h"
#include "../models/TaskRepository.h"

#include <memory>
#include <string_view>

namespace edu_schedule {

// ============================================================================
// TaskManagementTool: Quản lý vòng đời và cập nhật Task trong TaskRepository (TASK-26)
//
// Cung cấp các thao tác cập nhật dữ liệu (Mutation):
// - add: Thêm nhiệm vụ mới vào repository.
// - update_status: Chuyển đổi trạng thái (Todo -> InProgress -> Done).
// - update_remaining: Cập nhật thời gian còn lại của nhiệm vụ.
// - remove / delete: Xóa nhiệm vụ khỏi repository.
// ============================================================================
class TaskManagementTool final : public ITool {
public:
    explicit TaskManagementTool(std::shared_ptr<ITaskRepository> repository);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] std::string_view schema() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;

private:
    std::shared_ptr<ITaskRepository> repository_;
};

} // namespace edu_schedule
