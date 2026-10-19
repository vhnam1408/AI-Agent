
#pragma once

#include "Tool.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace edu {

class ToolRegistry {
public:
    template <ToolType T>
    void registerTool(std::unique_ptr<T> tool) {
        if (!tool) {
            throw std::invalid_argument("Tool cannot be null");
        }

        std::string key(tool->name());

        if (key.empty()) {
            throw std::invalid_argument(
                "Tool name cannot be empty"
            );
        }

        if (tools_.contains(key)) {
            throw std::runtime_error(
                "Duplicate tool: " + key
            );
        }

        std::unique_ptr<ITool> baseTool(std::move(tool));
        tools_.emplace(std::move(key), std::move(baseTool));
    }

    [[nodiscard]]
    ITool* findTool(std::string_view name) const {
        auto it = tools_.find(std::string(name));

        if (it == tools_.end()) {
            return nullptr;
        }

        return it->second.get();
    }

    [[nodiscard]]
    ToolResult execute(
        std::string_view name,
        std::string_view input
    ) {
        ITool* tool = findTool(name);

        if (tool == nullptr) {
            return {
                false,
                "Tool not found: " + std::string(name)
            };
        }

        return tool->execute(input);
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return tools_.size();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>>
        tools_;
};

} // namespace edu
