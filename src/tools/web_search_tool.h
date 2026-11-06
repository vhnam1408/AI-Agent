#pragma once

#include "../agent/Tool.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace edu_schedule {

// ============================================================================
// WebSearchTool: Công cụ tìm kiếm tài liệu học tập trực tuyến (TASK-20)
//
// Tên đăng ký trong ToolRegistry: "web_search"
// Đầu vào (input):
//   - Chuỗi truy vấn thuần: "C++20 std::format documentation"
//   - Hoặc JSON: {"query": "C++20 std::format", "limit": 3}
// Cơ chế dự phòng (Workaround):
//   - Gọi HTTP GET tới DuckDuckGo / SearXNG JSON API qua libcurl (RAII).
//   - Tự động chuyển sang đọc tệp JSON mẫu (mockJsonPath) khi mất mạng,
//     bị giới hạn API hoặc máy chủ không phản hồi để không làm gián đoạn test.
// ============================================================================
class WebSearchTool final : public ITool {
public:
    explicit WebSearchTool(
        std::filesystem::path mockJsonPath = "assets/mock_web_search.json",
        std::string endpointUrl = "https://api.duckduckgo.com/"
    );

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;

private:
    std::filesystem::path mockJsonPath_;
    std::string endpointUrl_;
};

} // namespace edu_schedule
