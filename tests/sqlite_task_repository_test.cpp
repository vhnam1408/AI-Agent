#include "models/sqlite_task_repository.h"
#include "tools/memory_tool.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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

bool sameTask(const std::optional<Task>& a, const Task& b) {
    return a.has_value() &&
           a->id == b.id &&
           a->title == b.title &&
           a->courseId == b.courseId &&
           a->deadline == b.deadline &&
           a->estimatedMinutes == b.estimatedMinutes &&
           a->remainingMinutes == b.remainingMinutes &&
           a->priority == b.priority &&
           a->status == b.status &&
           a->dependencies == b.dependencies;
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "=== [TASK-20 Part A] Unit Test: SqliteTaskRepository ===\n";

    std::error_code ec;
    const auto dbPath = std::filesystem::temp_directory_path() / "eduschedule_task20_test.db";
    const auto sharedDbPath = std::filesystem::temp_directory_path() / "eduschedule_task20_shared.db";
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(sharedDbPath, ec);

    const TimePoint baseDeadline = std::chrono::system_clock::now() + std::chrono::hours{24};

    const Task t1{"T1", "Thiết kế UML", "CS202", baseDeadline, 120, 90, Priority::High, TaskStatus::InProgress, {}};
    const Task t2{"T2", "Cài đặt SQLite Repo", "CS202", baseDeadline + std::chrono::hours{48}, 180, 180, Priority::Mid, TaskStatus::Todo, {"T1"}};
    const Task t3{"T3", "Ôn tập Giải tích", "MTH101", std::nullopt, 60, 0, Priority::Low, TaskStatus::Done, {}};

    // 1. Phiên 1: Validation, add, exists, count, findById
    {
        SqliteTaskRepository repo(dbPath);
        check(!repo.add(Task{}), "Phải từ chối Task có ID rỗng");
        check(!repo.add(Task{"BAD", "", "", std::nullopt, 30, 60, Priority::Mid, TaskStatus::Todo, {}}), "Phải từ chối remaining > estimated");

        const bool okAdd = repo.add(t1) && repo.add(t2) && repo.add(t3);
        check(okAdd && repo.count() == 3, "Thêm 3 Task hợp lệ thành công");
        check(!repo.add(t1), "Không cho phép thêm trùng ID");
        check(repo.exists("T1") && !repo.exists("NONE"), "Kiểm tra exists() chính xác");

        check(sameTask(repo.findById("T2"), t2), "findById() trả về đúng toàn bộ trường của Task");
    }
    std::cout << "  [PASS] 1. Validation & CRUD thêm mới / tra cứu\n";

    // 2. Phiên 2: Lưu trữ bền vững qua nhiều lần đóng/mở DB, lọc, sửa, xóa, chống SQL Injection
    check(std::filesystem::exists(dbPath), "Tệp .db phải tồn tại trên ổ cứng");
    {
        SqliteTaskRepository repo(dbPath);
        check(repo.count() == 3 && repo.findAll().size() == 3, "Dữ liệu bền vững sau khi mở lại tệp SQLite");
        check(repo.findByCourse("CS202").size() == 2, "findByCourse(CS202) trả về đúng 2 Task");
        check(repo.findByStatus(TaskStatus::Done).size() == 1, "findByStatus(Done) trả về đúng 1 Task");

        Task updated = t2;
        updated.title = "Hoàn thành TASK-20 Phần A";
        updated.status = TaskStatus::Done;
        updated.remainingMinutes = 0;
        const bool okUpdate = repo.update(updated);
        check(okUpdate && sameTask(repo.findById("T2"), updated), "update() cập nhật dữ liệu chính xác");
        check(!repo.update(Task{"GHOST", "", "", std::nullopt, 30, 30, Priority::Mid, TaskStatus::Todo, {}}), "update() từ chối ID lạ");

        const bool okRemove = repo.remove("T3");
        const bool okRemoveAgain = !repo.remove("T3");
        check(okRemove && okRemoveAgain && repo.count() == 2, "remove() xóa đúng Task");

        const Task sqli{"T'; DROP TABLE tasks;--", "Bảo mật ' OR '1'='1", "CS202", std::nullopt, 30, 15, Priority::Mid, TaskStatus::Todo, {}};
        const bool okSqli = repo.add(sqli);
        check(okSqli && sameTask(repo.findById(sqli.id), sqli), "Chống SQL Injection an toàn tuyệt đối");

        repo.clear();
        check(repo.count() == 0, "clear() làm sạch toàn bộ Task");
    }
    std::cout << "  [PASS] 2. Persistence, bộ lọc, update(), remove(), clear() & chống SQL Injection\n";

    // 3. Tương thích dùng chung tệp .db với MemorySaveTool (TASK-12) & đa hình ITaskRepository
    {
        MemorySaveTool memSave(sharedDbPath);
        const auto saveRes = memSave.execute("Ghi chú môn OOP");
        check(saveRes.success, "MemorySaveTool tạo DB trước thành công");

        std::unique_ptr<ITaskRepository> polyRepo = std::make_unique<SQLiteTaskRepository>(sharedDbPath);
        const bool okPoly = polyRepo->add(t1);
        check(okPoly && sameTask(polyRepo->findById("T1"), t1), "Đa hình ITaskRepository hoạt động tốt trên DB dùng chung");

        MemorySearchTool memSearch(sharedDbPath);
        const auto searchRes = memSearch.execute("OOP");
        check(searchRes.success && searchRes.message.find("OOP") != std::string::npos, "Bảng memories vẫn nguyên vẹn");
    }
    std::cout << "  [PASS] 3. Dùng chung tệp SQLite với MemoryTool & đa hình ITaskRepository\n";

    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(sharedDbPath, ec);
    std::cout << "=== ALL SQLITE TASK REPOSITORY TESTS PASSED! ===\n";
    return 0;
}
