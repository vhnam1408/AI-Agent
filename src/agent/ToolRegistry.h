#pragma once

#include "Tool.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace edu_schedule {

// Registry owns tools. A pointer returned by findTool() is borrowed and must
// not outlive the registry or the registered tool.
// Security default: every tool is denied until allowTool(name) is called.
class ToolRegistry {
public:
    ToolRegistry() = default;
    ~ToolRegistry() = default;
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;
    ToolRegistry(ToolRegistry&&) noexcept = default;
    ToolRegistry& operator=(ToolRegistry&&) noexcept = default;

    // Dynamic registration for a tool known only through the interface type.
    void registerTool(std::unique_ptr<ITool> tool);

    // Convenience overload for concrete tool types satisfying ToolType.
    template <ToolType T>
    void registerTool(std::unique_ptr<T> tool) {
        registerTool(std::unique_ptr<ITool>(std::move(tool)));
    }

    [[nodiscard]] ITool* findTool(std::string_view name);
    [[nodiscard]] const ITool* findTool(std::string_view name) const;

    void allowTool(std::string_view name);
    void denyTool(std::string_view name);
    void clearToolPolicy() noexcept;
    [[nodiscard]] bool isToolAllowed(std::string_view name) const;
    [[nodiscard]] ToolResult execute(std::string_view name, std::string_view input);

    [[nodiscard]] std::vector<std::string> listTools() const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>> tools_;
    std::unordered_set<std::string> allowedTools_;
    std::unordered_set<std::string> deniedTools_;
};

} // namespace edu_schedule
