#pragma once

#include "Tool.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - Registry sở hữu Tool bằng unique_ptr; findTool() trả con trỏ mượn.
// - Tool mới mặc định Denied; cấu hình tin cậy phải cho phép tường minh.
// - Agent phải gọi execute() qua Registry để kiểm tra quyền ở mỗi lần gọi.
// - findTool() chỉ tra cứu, không kiểm tra quyền và không dùng để bỏ qua execute().
// - Allow/deny không thay thế xác nhận của người dùng khi ghi dữ liệu.
// - Phiên bản MVP dùng Registry trên một luồng; chưa cam kết thread-safe.
class ToolRegistry {
public:
    template <ToolType T>
    void registerTool(std::unique_ptr<T> tool) {
        if (!tool) {
            throw std::invalid_argument("Tool cannot be null");
        }

        const std::string key(tool->name());
        if (key.empty()) {
            throw std::invalid_argument("Tool name cannot be empty");
        }
        if (tools_.contains(key)) {
            throw std::runtime_error("Duplicate tool: " + key);
        }

        std::unique_ptr<ITool> baseTool(std::move(tool));
        tools_.emplace(key, std::move(baseTool));
    }

    // Mutable lookup for a non-const registry.
    [[nodiscard]]
    ITool* findTool(std::string_view name) {
        const auto it = tools_.find(std::string(name));
        return it == tools_.end() ? nullptr : it->second.get();
    }

    // Const lookup does not expose a mutable ITool pointer.
    [[nodiscard]]
    const ITool* findTool(std::string_view name) const {
        const auto it = tools_.find(std::string(name));
        return it == tools_.end() ? nullptr : it->second.get();
    }

    // If the allow-list is empty, all registered tools are allowed unless denied.
    // If the allow-list is non-empty, only explicitly allowed tools may run.
    void allowTool(std::string_view name) {
        allowedTools_.emplace(name);
        deniedTools_.erase(std::string(name));
    }

    void denyTool(std::string_view name) {
        deniedTools_.emplace(name);
        allowedTools_.erase(std::string(name));
    }

    void clearToolPolicy() noexcept {
        allowedTools_.clear();
        deniedTools_.clear();
    }

    [[nodiscard]]
    bool isToolAllowed(std::string_view name) const {
        const std::string key(name);
        if (deniedTools_.contains(key)) {
            return false;
        }
        if (!allowedTools_.empty() && !allowedTools_.contains(key)) {
            return false;
        }
        return tools_.contains(key);
    }

    [[nodiscard]]
    ToolResult execute(std::string_view name, std::string_view input) {
        ITool* tool = findTool(name);
        if (tool == nullptr) {
            return {false, "Tool not found: " + std::string(name)};
        }
        if (!isToolAllowed(name)) {
            return {false, "Tool execution denied by policy: " + std::string(name)};
        }
        return tool->execute(input);
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return tools_.size();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>> tools_;
    std::unordered_set<std::string> allowedTools_;
    std::unordered_set<std::string> deniedTools_;
};

} // namespace edu_schedule