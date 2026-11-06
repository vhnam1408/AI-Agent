#pragma once

#include "../agent/Tool.h"

#include <filesystem>
#include <memory>
#include <string_view>

namespace edu_schedule {

// ============================================================================
// ICSExporterTool: Tool 8 xuất lịch học chuẩn RFC 5545 (.ics) kèm link tài liệu
//
// Tên đăng ký trong ToolRegistry: "ics_exporter"
// Đầu vào JSON cho execute():
//   {
//     "output_path": "schedule_output.ics",
//     "events": [
//       {
//         "uid": "oop-01@eduschedule.agent",
//         "summary": "Học OOP - Design Patterns",
//         "description": "Ôn tập Repository & Strategy",
//         "start_utc": "2026-11-09T01:00:00Z",
//         "end_utc": "2026-11-09T03:00:00Z",
//         "links": ["https://refactoring.guru/design-patterns"],
//         "search_query": "C++20"
//       }
//     ]
//   }
// ============================================================================
class ICSExporterTool final : public ITool {
public:
    explicit ICSExporterTool(
        std::filesystem::path defaultOutputPath = "schedule_output.ics",
        std::shared_ptr<ITool> webSearchTool = nullptr
    );

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;

private:
    std::filesystem::path defaultOutputPath_;
    std::shared_ptr<ITool> webSearchTool_;
};

using IcsExporterTool = ICSExporterTool;

} // namespace edu_schedule
