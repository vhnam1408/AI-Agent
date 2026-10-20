#pragma once

#include "../agent/Tool.h"
#include "../models/Task.h"

namespace edu_schedule {

class TaskLookupTool final : public ITool {
public:
    explicit TaskLookupTool(const Task& task);

    [[nodiscard]]
    std::string_view name() const noexcept override;

    [[nodiscard]]
    std::string_view description() const noexcept override;

    [[nodiscard]]
    ToolResult execute(std::string_view input) override;

private:
    const Task& task_;
};

} // namespace edu_schedule
