
#pragma once

#include "Tool.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace edu_schedule {

enum class ToolAccess {
    Denied,
    Allowed
};

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
    void registerTool(
        std::unique_ptr<T> tool,
        ToolAccess access = ToolAccess::Denied
    ) {
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

        tools_.emplace(std::move(key), Entry{std::move(tool), access});
    }

    [[nodiscard]]
    ITool* findTool(std::string_view name) {
        auto it = tools_.find(std::string(name));

        if (it == tools_.end()) {
            return nullptr;
        }

        return it->second.tool.get();
    }

    [[nodiscard]]
    const ITool* findTool(std::string_view name) const {
        auto it = tools_.find(std::string(name));
        return it == tools_.end() ? nullptr : it->second.tool.get();
    }

    // Trả false nếu Tool chưa đăng ký; không tự tạo Tool mới.
    [[nodiscard]]
    bool setToolAccess(std::string_view name, ToolAccess access) {
        auto it = tools_.find(std::string(name));
        if (it == tools_.end()) {
            return false;
        }
        it->second.access = access;
        return true;
    }

    [[nodiscard]]
    bool isToolAllowed(std::string_view name) const {
        auto it = tools_.find(std::string(name));
        return it != tools_.end() && it->second.access == ToolAccess::Allowed;
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

        if (!isToolAllowed(name)) {
            return {
                false,
                "Tool denied: " + std::string(name)
            };
        }

        // Không để một Tool lỗi làm sập AgentLoop.
        try {
            return tool->execute(input);
        } catch (const std::exception& error) {
            return {
                false,
                "Tool failed: " + std::string(error.what())
            };
        } catch (...) {
            return {
                false,
                "Tool failed: unknown exception"
            };
        }
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return tools_.size();
    }

private:
    struct Entry {
        std::unique_ptr<ITool> tool;
        ToolAccess access;
    };

    std::unordered_map<std::string, Entry> tools_;
};

} // namespace edu_schedule
