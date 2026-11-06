#include "tools/ics_exporter_tool.h"
#include "tools/web_search_tool.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace edu_schedule;

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "=== [TASK-24] Unit Test: ICSExporterTool + WebSearchTool ===\n";

    // Tự động tìm tệp Mock JSON dù chạy từ thư mục gốc dự án hay từ trong thư mục build/
    const std::filesystem::path mockSearchPath =
        std::filesystem::exists("assets/mock_web_search.json")
            ? std::filesystem::path("assets/mock_web_search.json")
            : std::filesystem::path("../assets/mock_web_search.json");
    auto webSearch = std::make_shared<WebSearchTool>(mockSearchPath, "");

    const std::filesystem::path outputIcsPath = "task24_schedule.ics";
    ICSExporterTool exporter(outputIcsPath, webSearch);

    // 1. Kiểm tra định danh ITool, bắt lỗi JSON và chặn Path Traversal
    check(exporter.name() == "ics_exporter", "Tên công cụ phải là 'ics_exporter'");
    check(!exporter.description().empty(), "Mô tả công cụ không được rỗng");
    check(!exporter.execute("").success, "Phải từ chối chuỗi rỗng");
    check(!exporter.execute("{invalid").success, "Phải từ chối JSON hỏng");
    check(!exporter.execute(R"({"events":[]})").success, "Phải từ chối danh sách events rỗng");
    check(!exporter.execute(R"({
        "output_path": "../outside.ics",
        "events": [{"summary": "Test", "start_utc": "2026-11-09T01:00:00Z", "end_utc": "2026-11-09T02:00:00Z"}]
    })").success, "Phải chặn Path Traversal '../' trong output_path");
    check(!exporter.execute(R"({
        "events": [{"summary": "Ngược giờ", "start_utc": "2026-11-09T05:00:00Z", "end_utc": "2026-11-09T02:00:00Z"}]
    })").success, "Phải từ chối sự kiện có start_utc >= end_utc");
    std::cout << "  [PASS] 1. Kiểm tra hợp đồng ITool, bắt lỗi dữ liệu và chặn Path Traversal\n";

    // 2. Xuất lịch học 3 sự kiện chuẩn RFC 5545 kèm link tài liệu từ JSON & WebSearchTool
    // Khung giờ theo giờ Việt Nam (UTC+7):
    // - Ca 1: Thứ Hai 09/11/2026, 08:00 - 10:00 sáng (01:00 - 03:00 UTC)
    // - Ca 2: Thứ Hai 09/11/2026, 14:00 - 16:00 chiều (07:00 - 09:00 UTC)
    // - Ca 3: Thứ Ba  10/11/2026, 19:00 - 21:00 tối  (12:00 - 14:00 UTC)
    const std::string validPayload = R"JSON({
        "output_path": "task24_schedule.ics",
        "events": [
            {
                "uid": "task24-oop-20261109@eduschedule.agent",
                "summary": "[TASK-24] Học OOP - Design Patterns & C++23",
                "description": "Ôn tập Repository Pattern và chuẩn bị báo cáo tuần.",
                "start_utc": "2026-11-09T01:00:00Z",
                "end_utc": "2026-11-09T03:00:00Z",
                "links": ["https://refactoring.guru/design-patterns"],
                "search_query": "cppreference"
            },
            {
                "uid": "task24-sqlite-20261109@eduschedule.agent",
                "summary": "[TASK-24] Thực hành SQLite & Prepared Statements",
                "description": "Kiểm thử lưu trữ bền vững SqliteTaskRepository.",
                "start_utc": "2026-11-09T07:00:00Z",
                "end_utc": "2026-11-09T09:00:00Z",
                "search_query": "SQLite"
            },
            {
                "uid": "task24-dag-20261110@eduschedule.agent",
                "summary": "[TASK-24] Giải thuật Đồ thị DAG & Topological Sort",
                "description": "Ôn tập thuật toán Kahn xếp thứ tự môn học.",
                "start_utc": "20261110T120000Z",
                "end_utc": "20261110T140000Z",
                "search_query": "Topological"
            }
        ]
    })JSON";

    const ToolResult exportRes = exporter.execute(validPayload);
    check(exportRes.success, "ICSExporterTool::execute() phải xuất lịch thành công: " + exportRes.message);
    check(std::filesystem::exists(outputIcsPath), "Tệp task24_schedule.ics phải được sinh ra trên đĩa");
    std::cout << "  [PASS] 2. Gọi ICSExporterTool::execute() kết hợp WebSearchTool thành công\n";

    // 3. Đọc nhị phân tệp .ics để kiểm chứng chuẩn RFC 5545, CRLF và các đường link tài liệu
    std::ifstream inFile(outputIcsPath, std::ios::binary);
    check(inFile.is_open(), "Phải mở đọc lại được tệp task24_schedule.ics");
    const std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    check(content.starts_with("BEGIN:VCALENDAR\r\n"), "Thiếu BEGIN:VCALENDAR chuẩn CRLF");
    check(content.ends_with("END:VCALENDAR\r\n"), "Thiếu END:VCALENDAR chuẩn CRLF");
    check(content.find("\r\r\n") == std::string::npos, "Không được bị lặp ký tự CR (\\r\\r\\n)");
    check(content.find("DTSTART:20261109T010000Z\r\n") != std::string::npos, "Sai DTSTART sự kiện 1");
    check(content.find("DTSTART:20261109T070000Z\r\n") != std::string::npos, "Sai DTSTART sự kiện 2");
    check(content.find("DTSTART:20261110T120000Z\r\n") != std::string::npos, "Sai DTSTART sự kiện 3");
    check(content.find("https://refactoring.guru/design-patterns") != std::string::npos,
          "Thiếu link tài liệu truyền trực tiếp trong JSON");
    check(content.find("https://en.cppreference.com/w/cpp") != std::string::npos,
          "Thiếu link C++ tự động lấy từ WebSearchTool");
    check(content.find("https://www.sqlite.org/c3ref/intro.html") != std::string::npos,
          "Thiếu link SQLite tự động lấy từ WebSearchTool");
    check(content.find("https://en.wikipedia.org/wiki/Topological_sorting") != std::string::npos,
          "Thiếu link DAG tự động lấy từ WebSearchTool");

    std::cout << "  [PASS] 3. Xác thực cấu trúc RFC 5545 (CRLF) & đính kèm đủ 4 link tài liệu học tập\n";
    std::cout << "  -> Đã xuất tệp lịch thực tế tại: " << std::filesystem::absolute(outputIcsPath).string() << "\n";
    std::cout << "=== ALL ICS EXPORTER TOOL TESTS PASSED! ===\n";
    return 0;
}
