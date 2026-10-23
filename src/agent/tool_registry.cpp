#include "ToolRegistry.h"

#include <algorithm>
#include <stdexcept>

namespace edu_schedule {

void ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
    if (!tool) {
        throw std::invalid_argument("ToolRegistry: tool cannot be null");
    }

    const std::string_view toolName = tool->name();
    if (toolName.empty()) {
        throw std::invalid_argument("ToolRegistry: tool name cannot be empty");
    }

    const std::string key(toolName);
    if (tools_.contains(key)) {
        throw std::runtime_error("ToolRegistry: duplicate tool: " + key);
    }

    // Move ownership into the registry. Newly registered tools remain denied
    // until explicitly allowed by policy.
    tools_.emplace(key, std::move(tool));
}

ITool* ToolRegistry::findTool(std::string_view name) {
    const auto it = tools_.find(std::string(name));
    return it == tools_.end() ? nullptr : it->second.get();
}

const ITool* ToolRegistry::findTool(std::string_view name) const {
    const auto it = tools_.find(std::string(name));
    return it == tools_.end() ? nullptr : it->second.get();
}

void ToolRegistry::allowTool(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("ToolRegistry: tool name cannot be empty");
    }
    const std::string key(name);
    deniedTools_.erase(key);
    allowedTools_.insert(key);
}

void ToolRegistry::denyTool(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("ToolRegistry: tool name cannot be empty");
    }
    const std::string key(name);
    allowedTools_.erase(key);
    deniedTools_.insert(key);
}

void ToolRegistry::clearToolPolicy() noexcept {
    allowedTools_.clear();
    deniedTools_.clear();
    // An empty allow-list means no tool has explicit permission: default deny.
}

bool ToolRegistry::isToolAllowed(std::string_view name) const {
    const std::string key(name);
    if (!tools_.contains(key) || deniedTools_.contains(key)) {
        return false;
    }
    return allowedTools_.contains(key);
}

ToolResult ToolRegistry::execute(std::string_view name, std::string_view input) {
    ITool* tool = findTool(name);
    if (tool == nullptr) {
        return {false, "Tool not found: " + std::string(name)};
    }
    if (!isToolAllowed(name)) {
        return {false, "Tool execution denied by policy: " + std::string(name)};
    }
    return tool->execute(input);
}

std::vector<std::string> ToolRegistry::listTools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        (void)tool;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::size_t ToolRegistry::size() const noexcept {
    return tools_.size();
}

} // namespace edu_schedule
