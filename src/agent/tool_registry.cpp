#include "ToolRegistry.h"

#include <algorithm>
#include <stdexcept>

namespace edu_schedule {

// Đăng ký một công cụ (tool) mới vào hệ thống quản lý registry.
// Nhận vào một std::unique_ptr<ITool> để chuyển giao toàn bộ quyền sở hữu (ownership) cho registry.
void ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
    // 1. Kiểm tra con trỏ tool truyền vào không được rỗng (null).
    if (!tool) {
        throw std::invalid_argument("ToolRegistry: tool cannot be null");
    }

    // 2. Kiểm tra tên của công cụ không được để trống.
    const std::string_view toolName = tool->name();
    if (toolName.empty()) {
        throw std::invalid_argument("ToolRegistry: tool name cannot be empty");
    }

    // 3. Kiểm tra xem công cụ với tên này đã tồn tại trong registry chưa để tránh đăng ký trùng lặp.
    const std::string key(toolName);
    if (tools_.contains(key)) {
        throw std::runtime_error("ToolRegistry: duplicate tool: " + key);
    }

    // Chuyển giao quyền sở hữu (move ownership) của công cụ vào registry.
    // Lưu ý về chính sách bảo mật: Công cụ mới đăng ký mặc định sẽ ở trạng thái bị từ chối (denied)
    // cho đến khi được cấp quyền rõ ràng thông qua hàm allowTool().
    tools_.emplace(key, std::move(tool));
}

// Tìm kiếm công cụ theo tên (phiên bản non-const).
// Trả về con trỏ thô (raw pointer) trỏ tới công cụ nếu tìm thấy, hoặc nullptr nếu không tồn tại.
// Lưu ý: Con trỏ trả về là con trỏ mượn (borrowed), vòng đời phụ thuộc vào registry.
ITool* ToolRegistry::findTool(std::string_view name) {
    const auto it = tools_.find(std::string(name));
    return it == tools_.end() ? nullptr : it->second.get();
}

// Tìm kiếm công cụ theo tên (phiên bản const).
// Trả về con trỏ const trỏ tới công cụ nếu tìm thấy, hoặc nullptr nếu không tồn tại.
const ITool* ToolRegistry::findTool(std::string_view name) const {
    const auto it = tools_.find(std::string(name));
    return it == tools_.end() ? nullptr : it->second.get();
}

// Cấp quyền cho phép một công cụ được thực thi (đưa vào whitelist).
// Nếu công cụ đang nằm trong danh sách cấm (deniedTools_), nó sẽ được gỡ khỏi danh sách cấm
// và thêm vào danh sách cho phép (allowedTools_).
void ToolRegistry::allowTool(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("ToolRegistry: tool name cannot be empty");
    }
    const std::string key(name);
    deniedTools_.erase(key);
    allowedTools_.insert(key);
}

// Từ chối hoặc thu hồi quyền thực thi của một công cụ (đưa vào blacklist).
// Gỡ công cụ khỏi danh sách cho phép (allowedTools_) và thêm vào danh sách cấm (deniedTools_).
void ToolRegistry::denyTool(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("ToolRegistry: tool name cannot be empty");
    }
    const std::string key(name);
    allowedTools_.erase(key);
    deniedTools_.insert(key);
}

// Xóa sạch toàn bộ chính sách công cụ (reset cả danh sách cho phép và danh sách từ chối).
// Áp dụng nguyên tắc "từ chối mặc định" (default-deny): khi danh sách cho phép rỗng,
// không có công cụ nào có quyền thực thi trừ khi được cấp quyền lại bằng allowTool().
void ToolRegistry::clearToolPolicy() noexcept {
    allowedTools_.clear();
    deniedTools_.clear();
}

// Kiểm tra xem một công cụ có được phép thực thi hay không theo chính sách bảo mật hiện tại.
// Điều kiện hợp lệ: công cụ phải tồn tại trong registry, KHÔNG nằm trong danh sách cấm (deniedTools_)
// và PHẢI nằm trong danh sách cho phép (allowedTools_).
bool ToolRegistry::isToolAllowed(std::string_view name) const {
    const std::string key(name);
    if (!tools_.contains(key) || deniedTools_.contains(key)) {
        return false;
    }
    return allowedTools_.contains(key);
}

// Thực thi công cụ với tên chỉ định và chuỗi dữ liệu đầu vào.
// Quy trình xử lý:
// 1. Tìm kiếm công cụ trong registry; nếu không tìm thấy, trả về lỗi "Tool not found".
// 2. Kiểm tra chính sách bảo mật; nếu công cụ bị cấm, trả về lỗi "Tool execution denied by policy".
// 3. Nếu hợp lệ, tiến hành gọi phương thức execute() của công cụ và trả về kết quả.
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

// Lấy danh sách tên của tất cả các công cụ đã được đăng ký trong registry.
// Danh sách trả về được sắp xếp theo thứ tự bảng chữ cái (alphabetical) để đảm bảo tính nhất quán.
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

// Trả về tổng số lượng công cụ hiện có trong registry.
std::size_t ToolRegistry::size() const noexcept {
    return tools_.size();
}

} // namespace edu_schedule
