#include "tools/memory_tool.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

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
    assert(std::filesystem::exists(testDbPath));
    assert(saveTool.hasTable("courses"));
    assert(saveTool.hasTable("tasks"));
    assert(saveTool.hasTable("task_dependencies"));
    assert(saveTool.hasTable("memories"));
    assert(saveTool.hasTable("memory_embeddings"));
    std::cout << "[PASS] 1. Khoi tao day du 5 bang SQLite theo schema TASK-04.\n";

    // 2. Kiểm tra lưu ký ức (memory_save) bằng văn bản thuần và bằng JSON
    const ToolResult save1 = saveTool.execute("Hạn nộp đồ án OOP là ngày 30/10/2026");
    assert(save1.success);

    const ToolResult save2 = saveTool.execute(R"({
        "memory_id": "mem_oop_01",
        "content": "Sinh viên cần thêm 25% thời gian cho bài tập C++ có SQLite",
        "source_type": "course",
        "source_id": "OOP"
    })");
    assert(save2.success);
    std::cout << "[PASS] 2. Luu ky uc (memory_save) vao SQLite thanh cong.\n";

    // 3. Kiểm tra truy vấn ký ức (memory_search) theo từ khóa
    const ToolResult search1 = searchTool.execute("30/10/2026");
    assert(search1.success);
    const auto json1 = nlohmann::json::parse(search1.message);
    assert(json1["count"].get<int>() == 1);
    assert(json1["results"][0]["content"].get<std::string>().find("30/10/2026") != std::string::npos);

    const ToolResult search2 = searchTool.execute(R"({"query": "SQLite", "limit": 5})");
    assert(search2.success);
    const auto json2 = nlohmann::json::parse(search2.message);
    assert(json2["count"].get<int>() == 1);
    assert(json2["results"][0]["memory_id"] == "mem_oop_01");
    std::cout << "[PASS] 3. Truy van ky uc (memory_search) chinh xac.\n";

    // 4. Kiểm tra chống SQL Injection và xử lý đầu vào rỗng
    const ToolResult sqlInjection = searchTool.execute("' OR '1'='1'; DROP TABLE memories; --");
    assert(sqlInjection.success);
    assert(saveTool.hasTable("memories"));
    assert(!saveTool.execute("   ").success);
    assert(!searchTool.execute("").success);
    std::cout << "[PASS] 4. Kiem tra Prepared Statement chong SQL Injection va bat loi dau vao.\n";

    // Dọn dẹp file database nháp sau khi test xong
    std::filesystem::remove(testDbPath, ec);
    std::cout << "=== TAT CA TEST TASK-12 DA VUOT QUA [100% PASS] ===\n";
    return 0;
}

