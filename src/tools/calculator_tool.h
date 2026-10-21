#pragma once

#include "../agent/Tool.h"

namespace edu_schedule {

class CalculatorTool final : public ITool {
public:
    [[nodiscard]]
    std::string_view name() const noexcept override;

    [[nodiscard]]
    std::string_view description() const noexcept override;

    [[nodiscard]]
    ToolResult execute(std::string_view input) override;
};

} // namespace edu_schedule