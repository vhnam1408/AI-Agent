#pragma once

#include "../agent/Tool.h"
#include "../harness/environment.h"

#include <memory>
#include <string_view>

namespace edu_schedule {

// ExecTool bản mẫu với allowlist thao tác nội bộ. Tool cố ý không gọi shell;
// input từ model chỉ chọn một thao tác đã định nghĩa, không thể truyền lệnh OS.
// Input JSON: {"command":"workspace_info"} hoặc
//             {"command":"list_files","path":"."}.
class ExecTool final : public ITool {
public:
    explicit ExecTool(std::shared_ptr<const IEnvironment> environment);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;

private:
    std::shared_ptr<const IEnvironment> environment_;
};

} // namespace edu_schedule
