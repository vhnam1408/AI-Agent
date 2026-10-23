#include "agent/ToolRegistry.h"
#include "tools/memory_tool.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    using namespace edu_schedule;

    const std::filesystem::path testDbPath =
        std::filesystem::temp_directory_path() / "eduschedule_task12_test_memory.db";

    std::error_code ec;
    std::filesystem::remove(testDbPath, ec);

    try {
        std::cout << "=== BẮT ĐẦU KIỂM THỬ TASK-12: SQLITE & MEMORY TOOL ===\n";

        // 1. Kiểm thử khởi tạo bảng SQLite theo schema TASK-04
        {
            MemoryTool memoryTool(testDbPath);
            check(std::filesystem::exists(testDbPath), "Database file must be created on disk");
            check(memoryTool.hasTable("courses"), "Missing table: courses");
            check(memoryTool.hasTable("tasks"), "Missing table: tasks");
            check(memoryTool.hasTable("task_dependencies"), "Missing table: task_dependencies");
            check(memoryTool.hasTable("memories"), "Missing table: memories");
            check(memoryTool.hasTable("memory_embeddings"), "Missing table: memory_embeddings");
            check(!memoryTool.hasTable("non_existent_table"), "Unexpected table reported as existing");
            std::cout << "[PASS] Khởi tạo đầy đủ 5 bảng SQLite theo schema TASK-04.\n";

            // 2. Kiểm thử lưu ký ức (memory_save) qua execute() JSON
            const ToolResult saveRes1 = memoryTool.execute(R"({
                "action": "memory_save",
                "memory_id": "mem_oop_01",
                "content": "Hạn nộp đồ án OOP là ngày 30/10/2026 lúc 23:59",
                "source_type": "course",
                "source_id": "OOP"
            })");
            check(saveRes1.success, "memory_save JSON 1 must succeed");
            const auto saveJson1 = nlohmann::json::parse(saveRes1.message);
            check(saveJson1["status"] == "success", "saveJson1 status must be success");
            check(saveJson1["data"]["saved"] == true, "saveJson1 saved flag must be true");
            check(saveJson1["data"]["memory_id"] == "mem_oop_01", "saveJson1 memory_id mismatch");

            // Lưu ký ức theo định dạng Trajectory mẫu của TASK-04 (course_id, task_id, deadline_utc)
            const ToolResult saveRes2 = memoryTool.execute(R"({
                "action": "memory_save",
                "course_id": "OOP",
                "course_name": "Lập trình hướng đối tượng",
                "task_id": "TASK_UML",
                "title": "Thiết kế sơ đồ lớp UML",
                "priority": 4,
                "status": "To-do",
                "estimated_duration": 120,
                "actual_duration": 150,
                "deadline_utc": "2026-10-30T16:59:59Z"
            })");
            check(saveRes2.success, "memory_save structured task JSON must succeed");

            // Lưu ký ức qua cú pháp tiền tố lệnh
            const ToolResult saveRes3 = memoryTool.execute(
                "memory_save: Sinh viên thường cần thêm 25% thời gian cho bài tập C++ có SQLite"
            );
            check(saveRes3.success, "memory_save prefix command must succeed");
            std::cout << "[PASS] Lưu ký ức và dữ liệu Task vào SQLite thành công.\n";

            // 3. Kiểm thử truy vấn ký ức (memory_search)
            const ToolResult searchRes1 = memoryTool.execute(R"({
                "action": "memory_search",
                "query": "30/10"
            })");
            check(searchRes1.success, "memory_search JSON must succeed");
            const auto searchJson1 = nlohmann::json::parse(searchRes1.message);
            check(searchJson1["status"] == "success", "searchJson1 status must be success");
            check(searchJson1["data"]["count"].get<int>() >= 1, "Must find at least 1 memory for '30/10'");
            check(
                searchJson1["data"]["results"][0]["content"].get<std::string>().find("30/10") != std::string::npos,
                "Returned memory content must contain '30/10'"
            );

            // Truy vấn qua tiền tố lệnh
            const ToolResult searchRes2 = memoryTool.execute("memory_search: SQLite");
            check(searchRes2.success, "memory_search prefix command must succeed");
            const auto searchJson2 = nlohmann::json::parse(searchRes2.message);
            check(searchJson2["data"]["count"].get<int>() == 1, "Must find 1 memory for 'SQLite'");

            // Kiểm tra an toàn SQL injection với chuỗi có dấu nháy đơn
            const ToolResult sqlInjectionSearch = memoryTool.execute(
                "memory_search: ' OR '1'='1'; DROP TABLE memories; --"
            );
            check(sqlInjectionSearch.success, "Prepared statement must safely handle special SQL characters");
            check(memoryTool.hasTable("memories"), "Table memories must remain intact");
            std::cout << "[PASS] Truy vấn memory_search và chống SQL Injection hoạt động chính xác.\n";
        }

        // 4. Kiểm thử tính bền vững (Persistence) và đăng ký vào ToolRegistry
        {
            ToolRegistry registry;
            registry.registerTool(std::make_unique<MemorySaveTool>(testDbPath));
            registry.registerTool(std::make_unique<MemorySearchTool>(testDbPath));

            registry.allowTool("memory_save");
            registry.allowTool("memory_search");

            // Dữ liệu từ phiên trước vẫn phải còn trên đĩa
            const ToolResult persistedSearch = registry.execute("memory_search", "OOP");
            check(persistedSearch.success, "Registry memory_search must succeed");
            const auto persistedJson = nlohmann::json::parse(persistedSearch.message);
            check(persistedJson["data"]["count"].get<int>() >= 2, "Persisted OOP memories must still exist");

            // Ghi mới qua MemorySaveTool trong ToolRegistry
            const ToolResult regSave = registry.execute(
                "memory_save",
                "Ghi chú: Khung giờ học hiệu quả nhất là 08:00-11:00"
            );
            check(regSave.success, "Registry memory_save plain text must succeed");

            const ToolResult regSearch = registry.execute("memory_search", "08:00-11:00");
            check(regSearch.success, "Registry memory_search must find newly saved note");
            const auto regSearchJson = nlohmann::json::parse(regSearch.message);
            check(regSearchJson["data"]["count"].get<int>() == 1, "Newly saved note must be returned");

            // Kiểm thử chặn quyền thực thi (denyTool)
            registry.denyTool("memory_save");
            check(
                !registry.execute("memory_save", "Dữ liệu không được phép lưu").success,
                "Denied memory_save must not execute"
            );
            std::cout << "[PASS] Tích hợp MemorySaveTool / MemorySearchTool với ToolRegistry thành công.\n";
        }

        // 5. Kiểm thử xử lý lỗi đầu vào không hợp lệ
        {
            MemoryTool memoryTool(testDbPath);
            check(!memoryTool.execute("").success, "Empty input must fail");
            check(!memoryTool.execute("   ").success, "Whitespace-only input must fail");
            check(!memoryTool.execute("{invalid_json").success, "Malformed JSON must fail");
            check(!memoryTool.execute(R"({"action": "memory_save", "content": "   "})").success, "Empty content must fail");
            check(!memoryTool.execute(R"({"action": "unknown_action"})").success, "Unknown action must fail");
            std::cout << "[PASS] Xử lý đầy đủ các trường hợp lỗi biên và đầu vào sai.\n";
        }

        std::filesystem::remove(testDbPath, ec);
        std::cout << "=== TẤT CẢ TEST TASK-12 ĐÃ VƯỢT QUA [100% PASS] ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::filesystem::remove(testDbPath, ec);
        std::cerr << "[FAIL] " << ex.what() << '\n';
        return 1;
    }
}

