
#pragma once

#include "../agent/Tool.h"
#include "../models/Task.h"

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - Tool mẫu chỉ tra một Task được truyền vào constructor.
// - Task được mượn và phải sống lâu hơn Tool; không nhận Task tạm.
// - input là TaskId thuần (ví dụ "T1"), chưa phải JSON; Tool chỉ đọc dữ liệu.
class TaskLookupTool final : public ITool {
public:
    explicit TaskLookupTool(const Task& task);
    TaskLookupTool(Task&&) = delete;
    TaskLookupTool(const Task&&) = delete;

    std::string_view name() const noexcept override;

    std::string_view description() const noexcept override;

    ToolResult execute(std::string_view input) override;

private:
    const Task& task_;
};

} // namespace edu_schedule
