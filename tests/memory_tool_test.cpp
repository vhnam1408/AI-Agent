#include "tools/memory_tool.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    using namespace edu_schedule;

    // Tạo file database nháp phục vụ kiểm thử độc lập (theo yêu cầu TASK-12)
    const std::filesystem::path testDbPath = "test_memory.db";
    std::error_code ec;
    std::filesystem::remove(testDbPath, ec);

    std::cout << "=== BAT DAU KIEM THU TASK-12: MEMORY TOOL & SQLITE ===\n";

    MemorySaveTool saveTool(testDbPath);
    MemorySearchTool searchTool(testDbPath);

    // 1. Kiểm tra đã tạo đầy đủ 5 bảng SQLite theo schema TASK-04
    check(std::filesystem::exists(testDbPath), "File test_memory.db khong ton tai");
    check(saveTool.hasTable("courses"), "Thieu bang courses");
    check(saveTool.hasTable("tasks"), "Thieu bang tasks");
    check(saveTool.hasTable("task_dependencies"), "Thieu bang task_dependencies");
    check(saveTool.hasTable("memories"), "Thieu bang memories");
    check(saveTool.hasTable("memory_embeddings"), "Thieu bang memory_embeddings");
    std::cout << "[PASS] 1. Khoi tao day du 5 bang SQLite theo schema TASK-04.\n";

    // 2. Kiểm tra lưu ký ức (memory_save) bằng văn bản thuần và bằng JSON
    const ToolResult save1 = saveTool.execute("Hạn nộp đồ án OOP là ngày 30/10/2026");
    check(save1.success, "Luu ky uc van ban thuan that bai");

    const ToolResult save2 = saveTool.execute(R"({
        "memory_id": "mem_oop_01",
        "content": "Sinh viên cần thêm 25% thời gian cho bài tập C++ có SQLite",
        "source_type": "course",
        "source_id": "OOP"
    })");
    check(save2.success, "Luu ky uc JSON that bai");
    std::cout << "[PASS] 2. Luu ky uc (memory_save) vao SQLite thanh cong.\n";

    // 3. Kiểm tra truy vấn ký ức (memory_search) theo từ khóa
    const ToolResult search1 = searchTool.execute("30/10/2026");
    check(search1.success, "Tim kiem ky uc 1 that bai");
    const auto json1 = nlohmann::json::parse(search1.message);
    check(json1["count"].get<int>() == 1, "So luong ket qua tim kiem 1 sai");
    check(
        json1["results"][0]["content"].get<std::string>().find("30/10/2026") != std::string::npos,
        "Noi dung tim kiem 1 sai");

    const ToolResult search2 = searchTool.execute(R"({"query": "SQLite", "limit": 5})");
    check(search2.success, "Tim kiem ky uc 2 that bai");
    const auto json2 = nlohmann::json::parse(search2.message);
    check(json2["count"].get<int>() == 1, "So luong ket qua tim kiem 2 sai");
    check(json2["results"][0]["memory_id"] == "mem_oop_01", "Ma memory_id tim kiem 2 sai");
    std::cout << "[PASS] 3. Truy van ky uc (memory_search) chinh xac.\n";

    // 4. Kiểm tra chống SQL Injection và xử lý đầu vào rỗng
    const ToolResult sqlInjection = searchTool.execute("' OR '1'='1'; DROP TABLE memories; --");
    check(sqlInjection.success, "Truy van SQL injection test that bai");
    check(saveTool.hasTable("memories"), "Bang memories bi xoa do SQL injection");
    const ToolResult emptySave = saveTool.execute("   ");
    check(!emptySave.success, "Khong bat loi dau vao rong cho memory_save");
    const ToolResult emptySearch = searchTool.execute("");
    check(!emptySearch.success, "Khong bat loi dau vao rong cho memory_search");
    std::cout << "[PASS] 4. Kiem tra Prepared Statement chong SQL Injection va bat loi dau vao.\n";

    // Dọn dẹp file database nháp sau khi test xong
    std::filesystem::remove(testDbPath, ec);
    std::cout << "=== TAT CA TEST TASK-12 DA VUOT QUA [100% PASS] ===\n";
    return 0;
}
