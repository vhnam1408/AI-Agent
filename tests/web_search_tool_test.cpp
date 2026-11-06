#include "tools/web_search_tool.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

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

void writeTextFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "=== [TASK-20 Part B & C] Unit Test: WebSearchTool ===\n";

    std::error_code ec;
    const auto searxMockPath = std::filesystem::temp_directory_path() / "eduschedule_task20_searx_mock.json";
    const auto ddgMockPath = std::filesystem::temp_directory_path() / "eduschedule_task20_ddg_mock.json";
    std::filesystem::remove(searxMockPath, ec);
    std::filesystem::remove(ddgMockPath, ec);

    writeTextFile(searxMockPath, R"JSON({
        "results": [
            {
                "title": "C++20 std::format Reference - cppreference",
                "url": "https://en.cppreference.com/w/cpp/utility/format/format",
                "snippet": "Format args according to the format string fmt."
            },
            {
                "title": "SQLite C/C++ Prepared Statements Guide",
                "url": "https://www.sqlite.org/c3ref/stmt.html",
                "snippet": "Using sqlite3_prepare_v2 and parameter binding to prevent SQL Injection."
            }
        ]
    })JSON");

    writeTextFile(ddgMockPath, R"JSON({
        "Heading": "Directed Acyclic Graph",
        "AbstractText": "Topological sorting using Kahn algorithm for task dependencies.",
        "AbstractURL": "https://duckduckgo.com/Directed_acyclic_graph",
        "RelatedTopics": [
            {
                "Text": "Kahn algorithm - Computes linear ordering of vertices in a DAG.",
                "FirstURL": "https://duckduckgo.com/Kahn_algorithm"
            }
        ]
    })JSON");

    // 1. Kiểm tra định danh ITool và từ chối đầu vào không hợp lệ
    {
        WebSearchTool tool(searxMockPath, "");
        check(tool.name() == "web_search", "Tên công cụ phải là 'web_search'");
        check(!tool.description().empty(), "Mô tả công cụ không được rỗng");

        check(!tool.execute("   ").success, "Phải từ chối truy vấn rỗng");
        check(!tool.execute("{bad_json").success, "Phải từ chối JSON sai cú pháp");
        check(!tool.execute(R"({"query": "  "})").success, "Phải từ chối trường query rỗng trong JSON");
    }
    std::cout << "  [PASS] 1. Kiểm tra hợp đồng ITool và bắt lỗi đầu vào rỗng / JSON hỏng\n";

    // 2. Giả lập đứt mạng / lỗi API qua libcurl -> Tự động chuyển sang đọc Mock JSON (SearXNG format)
    {
        WebSearchTool tool(searxMockPath, "http://127.0.0.1:1");

        const ToolResult resPlain = tool.execute("SQLite");
        check(resPlain.success, "Truy vấn từ khóa thuần phải thành công nhờ Mock Fallback");
        check(resPlain.message.find("source=mock_fallback") != std::string::npos &&
              resPlain.message.find("sqlite3_prepare_v2") != std::string::npos,
              "Kết quả phải lấy đúng mục SQLite từ tệp Mock JSON");

        const ToolResult resJson = tool.execute(R"({"query": "cppreference", "limit": 1})");
        check(resJson.success && resJson.message.find("std::format") != std::string::npos,
              "Truy vấn định dạng JSON kèm limit phải lọc đúng tài liệu C++20");
    }
    std::cout << "  [PASS] 2. Tự động Fallback từ libcurl sang Mock JSON (SearXNG schema) khi mất mạng\n";

    // 3. Kiểm tra phân tích cấu trúc JSON của DuckDuckGo Instant Answer API
    {
        WebSearchTool ddgTool(ddgMockPath, "");
        const ToolResult resDdg = ddgTool.execute("Kahn");
        check(resDdg.success &&
              resDdg.message.find("Directed Acyclic Graph") != std::string::npos &&
              resDdg.message.find("Kahn algorithm") != std::string::npos,
              "Phải bóc tách đúng AbstractText và RelatedTopics của DuckDuckGo JSON");

        WebSearchTool missingTool(std::filesystem::temp_directory_path() / "non_existent_mock_999.json", "");
        check(!missingTool.execute("test").success, "Phải trả về success=false khi mất mạng và không có tệp Mock");
    }
    std::cout << "  [PASS] 3. Phân tích cấu trúc DuckDuckGo Instant Answer JSON & xử lý khi thiếu tệp Mock\n";

    std::filesystem::remove(searxMockPath, ec);
    std::filesystem::remove(ddgMockPath, ec);
    std::cout << "=== ALL WEB SEARCH TOOL TESTS PASSED! ===\n";
    return 0;
}
