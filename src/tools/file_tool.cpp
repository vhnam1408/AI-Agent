#include "file_tool.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace edu_schedule {

namespace {

// Ngưỡng kích thước tệp tối đa cho phép đọc vào bộ nhớ (10 MB)
// Ngăn ngừa tấn công làm cạn kiệt bộ nhớ bằng file rác lớn.
constexpr std::size_t kMaxFileSizeBytes = 10 * 1024 * 1024;

// Hàm tiện ích: Cắt bỏ khoảng trắng thừa ở hai đầu chuỗi
std::string trim(std::string_view s) {
    const auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    const auto end = s.find_last_not_of(" \t\n\r");
    return std::string(s.substr(start, end - start + 1));
}

// Hàm tiện ích: Trích xuất đường dẫn tệp một cách linh hoạt
// Hỗ trợ cả định dạng JSON: {"path": "..."} và văn bản thuần: "data/input.txt"
std::string extractPath(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.starts_with('{')) {
        try {
            const auto j = nlohmann::json::parse(trimmed);
            if (j.contains("path") && j["path"].is_string()) {
                return j["path"].get<std::string>();
            }
        } catch (...) {
            // Khi parse JSON không thành công, giữ nguyên chuỗi thô để dùng làm đường dẫn trực tiếp
        }
    }
    return trimmed;
}

} // namespace

// ============================================================================
// Hiện thực FileToolBase
// ============================================================================

FileToolBase::FileToolBase(std::filesystem::path workspaceRoot)
    : workspaceRoot_(std::filesystem::weakly_canonical(std::move(workspaceRoot))) {}

const std::filesystem::path& FileToolBase::getWorkspaceRoot() const noexcept {
    return workspaceRoot_;
}

void FileToolBase::setWorkspaceRoot(std::filesystem::path newRoot) {
    // Chuẩn hóa đường dẫn ngay khi cập nhật để các phép so sánh luôn chính xác
    workspaceRoot_ = std::filesystem::weakly_canonical(std::move(newRoot));
}

std::filesystem::path FileToolBase::resolvePath(const std::filesystem::path& targetPath) const {
    // Nếu là đường dẫn tuyệt đối, giữ nguyên; nếu là đường dẫn tương đối, ghép vào workspaceRoot
    if (targetPath.is_absolute()) {
        return targetPath;
    }
    return workspaceRoot_ / targetPath;
}

bool FileToolBase::isPathSafe(const std::filesystem::path& targetPath) const {
    try {
        // Chuyển thành đường dẫn tuyệt đối
        const auto absolutePath = resolvePath(targetPath);

        // weakly_canonical: Phân giải các thành phần `.` và `..` ngay cả khi file chưa tồn tại
        const auto canonicalTarget = std::filesystem::weakly_canonical(absolutePath);
        const auto canonicalRoot = std::filesystem::weakly_canonical(workspaceRoot_);

        // Chuẩn hóa chuỗi biểu diễn đường dẫn theo cú pháp chuẩn của hệ điều hành
        const auto rootStr = canonicalRoot.lexically_normal().string();
        const auto targetStr = canonicalTarget.lexically_normal().string();

        // Đường dẫn an toàn khi và chỉ khi nó là đường con của workspaceRoot
        return targetStr.starts_with(rootStr);
    } catch (...) {
        // Mọi ngoại lệ phát sinh trong quá trình chuẩn hóa đều bị coi là không an toàn
        return false;
    }
}

// ============================================================================
// Hiện thực ReadFileTool
// ============================================================================

ReadFileTool::ReadFileTool(std::filesystem::path workspaceRoot)
    : FileToolBase(std::move(workspaceRoot)) {}

std::string_view ReadFileTool::name() const noexcept {
    return "read_file";
}

std::string_view ReadFileTool::description() const noexcept {
    return "Read content from a file within workspace. Input: file path (plain text or JSON {\"path\": \"...\"}).";
}

ToolResult ReadFileTool::execute(std::string_view input) {
    // Trích xuất đường dẫn tệp từ input
    const std::string pathStr = extractPath(input);
    if (pathStr.empty()) {
        return {false, "Error: File path is empty."};
    }

    // Kiểm tra bảo mật Path Traversal
    const std::filesystem::path targetPath(pathStr);
    if (!isPathSafe(targetPath)) {
        return {false, "Access Denied: Path traversal detected or path outside workspace: " + pathStr};
    }

    // Kiểm tra sự tồn tại và loại tệp
    const auto resolved = resolvePath(targetPath);
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec) || !std::filesystem::is_regular_file(resolved, ec)) {
        return {false, "Error: File does not exist or is not a regular file: " + pathStr};
    }

    // Kiểm tra giới hạn dung lượng tệp
    const auto fileSize = std::filesystem::file_size(resolved, ec);
    if (!ec && fileSize > kMaxFileSizeBytes) {
        return {false, "Error: File size exceeds safety limit of 10 MB."};
    }

    // Mở tệp và đọc dữ liệu qua luồng binary
    std::ifstream file(resolved, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {false, "Error: Failed to open file for reading: " + pathStr};
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return {true, ss.str()};
}

// ============================================================================
// Hiện thực WriteFileTool
// ============================================================================

WriteFileTool::WriteFileTool(std::filesystem::path workspaceRoot)
    : FileToolBase(std::move(workspaceRoot)) {}

std::string_view WriteFileTool::name() const noexcept {
    return "write_file";
}

std::string_view WriteFileTool::description() const noexcept {
    return "Write content to a file within workspace. Input: JSON {\"path\": \"...\", \"content\": \"...\", \"append\": false}.";
}

ToolResult WriteFileTool::execute(std::string_view input) {
    // Kiểm tra tính hợp lệ của chuỗi đầu vào
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return {false, "Error: Input is empty. Expected JSON: {\"path\": \"...\", \"content\": \"...\"}."};
    }

    std::string pathStr;
    std::string content;
    bool append = false;

    // Phân tích cú pháp JSON
    try {
        const auto j = nlohmann::json::parse(trimmed);
        if (!j.contains("path") || !j["path"].is_string()) {
            return {false, "Error: JSON must contain a string field 'path'."};
        }
        pathStr = j["path"].get<std::string>();

        if (j.contains("content") && j["content"].is_string()) {
            content = j["content"].get<std::string>();
        }
        if (j.contains("append") && j["append"].is_boolean()) {
            append = j["append"].get<bool>();
        }
    } catch (const std::exception& e) {
        return {false, std::string("Error parsing JSON input: ") + e.what() + ". Format: {\"path\": \"...\", \"content\": \"...\"}"};
    }

    if (pathStr.empty()) {
        return {false, "Error: File path is empty."};
    }

    // Kiểm tra bảo mật Path Traversal
    const std::filesystem::path targetPath(pathStr);
    if (!isPathSafe(targetPath)) {
        return {false, "Access Denied: Path traversal detected or path outside workspace: " + pathStr};
    }

    const auto resolved = resolvePath(targetPath);
    std::error_code ec;

    // Tự động khởi tạo cấu trúc thư mục cha nếu chưa tồn tại
    if (resolved.has_parent_path()) {
        std::filesystem::create_directories(resolved.parent_path(), ec);
        if (ec) {
            return {false, "Error: Failed to create parent directories: " + ec.message()};
        }
    }

    // Xác định chế độ ghi (ghi đè hoặc ghi nối tiếp)
    std::ios_base::openmode mode = std::ios::out | std::ios::binary;
    if (append) {
        mode |= std::ios::app;
    } else {
        mode |= std::ios::trunc;
    }

    // Mở luồng và ghi dữ liệu ra tệp
    std::ofstream file(resolved, mode);
    if (!file.is_open()) {
        return {false, "Error: Failed to open file for writing: " + pathStr};
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        return {false, "Error: Failed while writing to file: " + pathStr};
    }

    return {true, "Successfully wrote " + std::to_string(content.size()) + " bytes to " + pathStr};
}

} // namespace edu_schedule
