
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <utility>

#include "Tool.h"

class ToolRegistry {
private:
    std::unordered_map<
        std::string,
        std::unique_ptr<ITool>
    > tools;

public:
    void registerTool(std::unique_ptr<ITool> tool) {
        if (!tool) {
            throw std::invalid_argument("Null tool");
        }

        std::string key = tool->name();

        if (tools.count(key) > 0) {
            throw std::runtime_error("Duplicate tool");
        }

        tools.emplace(key, std::move(tool));
    }

    ITool* findTool(const std::string& name) const {
        auto it = tools.find(name);

        if (it == tools.end()) {
            return nullptr;
        }

        return it->second.get();
    }
};
