
#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC: caller phải kiểm tra success trước khi dùng message.
// message sở hữu dữ liệu kết quả; không trả string_view trỏ tới dữ liệu tạm.
struct ToolResult {
    bool success = false;
    std::string message;
};

class ITool {
public:
    virtual ~ITool() = default;

    // name không rỗng, duy nhất và không đổi sau khi đăng ký.
    // name()/description() phải trả view còn sống suốt vòng đời Tool
    // (string literal hoặc chuỗi thành viên ổn định, không dùng chuỗi cục bộ).
    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view description() const noexcept = 0;

    // input chỉ được mượn trong lời gọi; Tool muốn giữ lại phải tự copy.
    // Lỗi nghiệp vụ trả success=false. Tool ghi dữ liệu vẫn phải tuân thủ
    // Preview -> Confirm -> Commit; quyền chạy Tool không thay thế confirm.
    virtual ToolResult execute(std::string_view input) = 0;
};

template <typename T>
concept ToolType =
    std::derived_from<T, ITool> &&
    !std::is_abstract_v<T>;

} // namespace edu_schedule
