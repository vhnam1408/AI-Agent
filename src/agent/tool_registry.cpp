#include "ToolRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace edu_schedule {
namespace {

using json = nlohmann::json;

} // namespace

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

bool ToolRegistry::hasTool(std::string_view name) const {
    return tools_.contains(std::string(name));
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

void ToolRegistry::allowAllTools() {
    deniedTools_.clear();
    for (const auto& [name, _] : tools_) {
        allowedTools_.insert(name);
    }
}

void ToolRegistry::denyAllTools() {
    allowedTools_.clear();
    for (const auto& [name, _] : tools_) {
        deniedTools_.insert(name);
    }
}

void ToolRegistry::clearToolPolicy() noexcept {
    allowedTools_.clear();
    deniedTools_.clear();
}

bool ToolRegistry::isToolAllowed(std::string_view name) const {
    const std::string key(name);
    if (!tools_.contains(key) || deniedTools_.contains(key)) {
        return false;
    }
    return allowedTools_.contains(key);
}

ToolResult ToolRegistry::execute(std::string_view name, std::string_view input) {
    const auto start = std::chrono::steady_clock::now();

    ITool* tool = findTool(name);
    if (tool == nullptr) {
        ToolResult res{false, "Tool not found: " + std::string(name)};
        if (executionHook_) {
            executionHook_(name, input, res, 0);
        }
        return res;
    }

    if (!isToolAllowed(name)) {
        ToolResult res{false, "Tool execution denied by policy: " + std::string(name)};
        if (executionHook_) {
            executionHook_(name, input, res, 0);
        }
        return res;
    }

    ToolResult result;
    try {
        result = tool->execute(input);
    } catch (const std::exception& error) {
        result = {false, "Tool exception: " + std::string(error.what())};
    } catch (...) {
        result = {false, "Tool exception: unknown error"};
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Tự động đồng bộ kết quả vào TaskRepository nếu có dữ liệu hợp lệ
    if (result.success && autoSyncResults_ && taskRepository_) {
        syncToolResultToRepository(result.message);
    }

    // Thông báo cho Execution Hook
    if (executionHook_) {
        executionHook_(name, input, result, durationMs);
    }

    return result;
}

std::vector<std::string> ToolRegistry::listTools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> ToolRegistry::listAllowedTools() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : tools_) {
        if (isToolAllowed(name)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::size_t ToolRegistry::size() const noexcept {
    return tools_.size();
}

std::string ToolRegistry::exportToolsSchemaJson() const {
    json arr = json::array();
    const auto names = listTools();
    for (const auto& name : names) {
        const auto* tool = findTool(name);
        if (!tool) continue;

        json item;
        item["name"] = std::string(tool->name());
        item["description"] = std::string(tool->description());
        item["allowed"] = isToolAllowed(tool->name());

        std::string schemaStr(tool->schema());
        try {
            item["schema"] = json::parse(schemaStr);
        } catch (...) {
            item["schema"] = schemaStr;
        }
        arr.push_back(item);
    }
    return arr.dump(2);
}

void ToolRegistry::setTaskRepository(std::shared_ptr<ITaskRepository> repository) {
    taskRepository_ = std::move(repository);
}

std::shared_ptr<ITaskRepository> ToolRegistry::getTaskRepository() const noexcept {
    return taskRepository_;
}

bool ToolRegistry::hasTaskRepository() const noexcept {
    return taskRepository_ != nullptr;
}

void ToolRegistry::setAutoSyncResults(bool enable) noexcept {
    autoSyncResults_ = enable;
}

bool ToolRegistry::isAutoSyncResults() const noexcept {
    return autoSyncResults_;
}

void ToolRegistry::setExecutionHook(ToolExecutionHook hook) {
    executionHook_ = std::move(hook);
}

void ToolRegistry::syncToolResultToRepository(std::string_view outputJson) {
    if (!taskRepository_) return;

    try {
        const auto parsed = json::parse(outputJson);
        if (!parsed.is_object()) return;

        // Nhận diện mảng "subtasks" hoặc "tasks"
        const json* taskArray = nullptr;
        if (parsed.contains("subtasks") && parsed["subtasks"].is_array()) {
            taskArray = &parsed["subtasks"];
        } else if (parsed.contains("tasks") && parsed["tasks"].is_array()) {
            taskArray = &parsed["tasks"];
        }

        if (!taskArray) return;

        for (const auto& item : *taskArray) {
            if (!item.is_object()) continue;
            if (!item.contains("id") || !item["id"].is_string()) continue;

            Task task;
            task.id = item["id"].get<std::string>();
            task.title = item.value("title", task.id);
            task.courseId = item.value("course_id", "");
            task.estimatedMinutes = item.value("estimated_minutes", 0);
            task.remainingMinutes = item.value("remaining_minutes", task.estimatedMinutes);

            if (item.contains("dependencies") && item["dependencies"].is_array()) {
                for (const auto& dep : item["dependencies"]) {
                    if (dep.is_string()) {
                        task.dependencies.push_back(dep.get<std::string>());
                    }
                }
            }

            if (taskRepository_->exists(task.id)) {
                taskRepository_->update(task);
            } else {
                taskRepository_->add(task);
            }
        }
    } catch (...) {
        // Bỏ qua lỗi parse nếu output không phải định dạng JSON tương thích
    }
}

} // namespace edu_schedule
