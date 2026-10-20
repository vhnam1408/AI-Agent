
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace edu_schedule {

// Skill là nội dung hướng dẫn, không phải ITool và không tự cấp quyền chạy Tool.
struct Skill {
    // name dùng tên file kèm .md và cũng là key trong kết quả loadAll().
    std::string name;
    std::string content;
    std::filesystem::path source;
};

// QUY ƯỚC BẮT BUỘC (giao diện đã chốt, phần đọc file triển khai sau):
// - load chỉ nhận tên file .md nằm trong skillDirectory.
// - Khi triển khai phải chặn "..", đường dẫn tuyệt đối và symlink thoát thư mục.
// - load trả nullopt nếu không đọc được/không hợp lệ; Skill sở hữu content.
// - loadAll trả map theo tên file; thư mục không đọc được trả map rỗng.
// - Không thực thi lệnh Markdown, không tự đăng ký hay cấp quyền cho Tool.
class SkillLoader {
public:
    explicit SkillLoader(
        std::filesystem::path skillDirectory
    );

    [[nodiscard]]
    std::optional<Skill> load(
        std::string_view filename
    ) const;

    [[nodiscard]]
    std::unordered_map<std::string, Skill> loadAll() const;

private:
    std::filesystem::path skillDirectory_;
};

} // namespace edu_schedule
