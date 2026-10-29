#pragma once

#include "../agent/Tool.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace edu_schedule {

// FileToolBase: Lớp cơ sở quản lý không gian làm việc (workspace boundary)

// QUY ƯỚC BẮT BUỘC:
// - Mọi thao tác tệp tin của Agent PHẢI bị giới hạn bên trong workspaceRoot_.
// - Chặn tuyệt đối tấn công vượt quyền (Path Traversal) như `../` hoặc đường dẫn
//   tuyệt đối trỏ ra ngoài thư mục được phép (ví dụ: C:\Windows, /etc/passwd).
// - Đường dẫn tương đối sẽ được tự động ánh xạ vào thư mục gốc của workspace.
class FileToolBase {
public:
    // Khởi tạo với workspaceRoot mặc định là thư mục hiện hành, hoặc thư mục chỉ định
    explicit FileToolBase(std::filesystem::path workspaceRoot = std::filesystem::current_path());
    virtual ~FileToolBase() = default;

    // Lấy đường dẫn thư mục gốc của workspace
    [[nodiscard]] const std::filesystem::path& getWorkspaceRoot() const noexcept;

    // Cập nhật thư mục gốc của workspace (hữu ích cho SandboxEnvironment và Unit Test)
    void setWorkspaceRoot(std::filesystem::path newRoot);

    // Kiểm tra an toàn:
    // Dùng std::filesystem::weakly_canonical để phân giải các ký hiệu `.` và `..`.
    // Dùng lexically_normal() để chuẩn hóa chuỗi đường dẫn.
    // Trả về true nếu đường dẫn đích nằm hoàn toàn bên trong workspaceRoot_.
    [[nodiscard]] bool isPathSafe(const std::filesystem::path& targetPath) const;

    // Chuyển đổi đường dẫn tương đối thành đường dẫn tuyệt đối bên trong workspace
    [[nodiscard]] std::filesystem::path resolvePath(const std::filesystem::path& targetPath) const;

protected:
    std::filesystem::path workspaceRoot_;
};

// ReadFileTool: Đọc nội dung tệp tin an toàn

// Kế thừa: ITool (giao diện thực thi cho Agent) và FileToolBase (bảo vệ workspace).
// Tên đăng ký Registry: "read_file"
// Đầu vào (input): Chuỗi đường dẫn tệp (văn bản thuần hoặc JSON {"path": "..."}).
// Ràng buộc:
// - Tệp phải tồn tại và là tệp thông thường (regular file).
// - Giới hạn kích thước tối đa (10 MB) để tránh cạn kiệt bộ nhớ.
// - Trả về ToolResult{false, error} nếu không an toàn hoặc lỗi đọc tệp.
class ReadFileTool final : public ITool, public FileToolBase {
public:
    explicit ReadFileTool(std::filesystem::path workspaceRoot = std::filesystem::current_path());

    // Tên công cụ đăng ký trong ToolRegistry (cố định: "read_file")
    [[nodiscard]] std::string_view name() const noexcept override;

    // Mô tả công cụ để LLM hiểu cách sử dụng và định dạng tham số
    [[nodiscard]] std::string_view description() const noexcept override;

    // Thực thi đọc tệp: kiểm tra path traversal -> mở file qua ifstream -> trả nội dung
    [[nodiscard]] ToolResult execute(std::string_view input) override;
};

// WriteFileTool: Ghi nội dung tệp tin an toàn

// Kế thừa: ITool và FileToolBase.
// Tên đăng ký Registry: "write_file"
// Đầu vào (input): Chuỗi JSON có cấu trúc:
//   {
//     "path": "duong_dan_file.txt",
//     "content": "noi_dung_can_ghi",
//     "append": false   // (Tùy chọn: true để ghi nối tiếp, false để ghi đè)
//   }
// Ràng buộc:
// - Tự động tạo cây thư mục cha nếu chưa có (create_directories).
// - Ghi dữ liệu bằng std::ofstream chuẩn RAII, tự động xả bộ đệm và đóng luồng.

class WriteFileTool final : public ITool, public FileToolBase {
public:
    explicit WriteFileTool(std::filesystem::path workspaceRoot = std::filesystem::current_path());

    // Tên công cụ đăng ký trong ToolRegistry (cố định: "write_file")
    [[nodiscard]] std::string_view name() const noexcept override;

    // Mô tả công cụ để LLM hiểu định dạng JSON yêu cầu
    [[nodiscard]] std::string_view description() const noexcept override;

    // Thực thi ghi tệp: parse JSON -> kiểm tra path traversal -> tạo thư mục -> ghi nội dung
    [[nodiscard]] ToolResult execute(std::string_view input) override;
};

} // namespace edu_schedule
