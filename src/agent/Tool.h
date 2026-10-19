
#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

namespace edu {

struct ToolResult {
    bool success = false;
    std::string message;
};

class ITool {
public:
    virtual ~ITool() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view description() const noexcept = 0;

    virtual ToolResult execute(std::string_view input) = 0;
};

template <typename T>
concept ToolType =
    std::derived_from<T, ITool> &&
    !std::is_abstract_v<T>;

} // namespace edu
