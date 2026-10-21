#include "agent/ToolRegistry.h"
#include "tools/calculator_tool.h"

#include <iostream>
#include <memory>
#include <string_view>

namespace {

bool expectResult(
    edu_schedule::CalculatorTool& tool,
    std::string_view input,
    std::string_view expected
) {
    const edu_schedule::ToolResult result =
        tool.execute(input);

    return result.success && result.message == expected;
}

bool expectFailure(
    edu_schedule::CalculatorTool& tool,
    std::string_view input
) {
    return !tool.execute(input).success;
}

} // namespace

int main() {
    using namespace edu_schedule;

    CalculatorTool calculator;

    bool passed = true;

    passed &= expectResult(calculator, "15 * 17", "255");
    passed &= expectResult(calculator, "(2 + 3) * 4", "20");
    passed &= expectResult(calculator, "2 ^ 3", "8");

    passed &= expectResult(
        calculator,
        "compensation_factor(120, 150)",
        "1.25"
    );

    passed &= expectResult(
        calculator,
        "adjusted_minutes(120, 1.25)",
        "150"
    );

    passed &= expectFailure(calculator, "");
    passed &= expectFailure(calculator, "2 +");
    passed &= expectFailure(calculator, "10 / 0");
    passed &= expectFailure(calculator, "unknown(1, 2)");

    ToolRegistry registry;

    // Mặc định phải bị từ chối.
    registry.registerTool(
        std::make_unique<CalculatorTool>()
    );

    passed &= !registry.execute(
        "calculator",
        "6 * 7"
    ).success;

    passed &= registry.setToolAccess(
        "calculator",
        ToolAccess::Allowed
    );

    const ToolResult registryResult =
        registry.execute("calculator", "6 * 7");

    passed &= registryResult.success;
    passed &= registryResult.message == "42";

    if (!passed) {
        std::cerr << "CalculatorTool tests failed\n";
        return 1;
    }

    std::cout << "CalculatorTool tests passed\n";
    return 0;
}