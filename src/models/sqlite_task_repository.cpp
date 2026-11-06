#include "sqlite_task_repository.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <charconv>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace edu_schedule {

namespace {

struct StmtDeleter {
    void operator()(sqlite3_stmt* stmt) const noexcept {
        if (stmt != nullptr) sqlite3_finalize(stmt);
    }
};
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

bool execSql(sqlite3* db, const char* sql) noexcept {
    return db != nullptr && sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

int bindText(sqlite3_stmt* stmt, int index, std::string_view text) noexcept {
    const char* ptr = text.empty() ? "" : text.data();
    return sqlite3_bind_text(stmt, index, ptr, static_cast<int>(text.size()), SQLITE_TRANSIENT);
}

std::string colText(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) return {};
    const auto* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    const int len = sqlite3_column_bytes(stmt, col);
    return (raw != nullptr && len > 0) ? std::string(raw, static_cast<std::size_t>(len)) : std::string{};
}

bool hasColumn(sqlite3* db, std::string_view colName) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(tasks);", -1, &raw, nullptr) != SQLITE_OK) return false;
    StmtPtr stmt(raw);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        if (colText(stmt.get(), 1) == colName) return true;
    }
    return false;
}

bool initSchema(sqlite3* db) {
    const char* sql = R"SQL(
CREATE TABLE IF NOT EXISTS courses (
    course_id   TEXT PRIMARY KEY,
    course_name TEXT NOT NULL,
    lecturer    TEXT
);
CREATE TABLE IF NOT EXISTS tasks (
    task_id             TEXT PRIMARY KEY,
    course_id           TEXT,
    title               TEXT NOT NULL,
    description         TEXT,
    priority            INTEGER NOT NULL DEFAULT 3 CHECK (priority BETWEEN 1 AND 5),
    status              TEXT NOT NULL DEFAULT 'To-do'
                        CHECK (status IN ('To-do', 'In-Progress', 'Done', 'Cancelled', 'Blocked')),
    estimated_duration  INTEGER CHECK (estimated_duration IS NULL OR estimated_duration >= 0),
    actual_duration     INTEGER CHECK (actual_duration IS NULL OR actual_duration >= 0),
    remaining_duration  INTEGER NOT NULL DEFAULT 0 CHECK (remaining_duration >= 0),
    deadline_utc        TEXT,
    dependencies_json   TEXT NOT NULL DEFAULT '[]',
    created_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    FOREIGN KEY (course_id) REFERENCES courses(course_id) ON DELETE CASCADE
);
)SQL";
    if (!execSql(db, sql)) return false;

    // Bổ sung cột nếu tệp .db được tạo trước bởi MemoryToolBase (TASK-12)
    if (!hasColumn(db, "remaining_duration") &&
        !execSql(db, "ALTER TABLE tasks ADD COLUMN remaining_duration INTEGER NOT NULL DEFAULT 0;")) {
        return false;
    }
    if (!hasColumn(db, "dependencies_json") &&
        !execSql(db, "ALTER TABLE tasks ADD COLUMN dependencies_json TEXT NOT NULL DEFAULT '[]';")) {
        return false;
    }
    return true;
}

int priorityToSql(Priority p) noexcept {
    switch (p) {
    case Priority::Low:  return 1;
    case Priority::Mid:  return 3;
    case Priority::High: return 5;
    }
    return 3;
}

Priority priorityFromSql(int v) noexcept {
    if (v <= 2) return Priority::Low;
    if (v >= 4) return Priority::High;
    return Priority::Mid;
}

std::string_view statusToSql(TaskStatus s) noexcept {
    switch (s) {
    case TaskStatus::Todo:       return "To-do";
    case TaskStatus::InProgress: return "In-Progress";
    case TaskStatus::Done:       return "Done";
    case TaskStatus::Blocked:    return "Blocked";
    case TaskStatus::Cancelled:  return "Cancelled";
    }
    return "To-do";
}

TaskStatus statusFromSql(std::string_view s) noexcept {
    if (s == "In-Progress") return TaskStatus::InProgress;
    if (s == "Done")        return TaskStatus::Done;
    if (s == "Blocked")     return TaskStatus::Blocked;
    if (s == "Cancelled")   return TaskStatus::Cancelled;
    return TaskStatus::Todo;
}

bool ensureCourse(sqlite3* db, std::string_view courseId) {
    if (courseId.empty()) return true;
    sqlite3_stmt* raw = nullptr;
    const char* sql = "INSERT OR IGNORE INTO courses (course_id, course_name) VALUES (?1, ?1);";
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) return false;
    StmtPtr stmt(raw);
    return bindText(stmt.get(), 1, courseId) == SQLITE_OK && sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool executeWrite(sqlite3* db, const char* sql, const Task& task, bool requireChanged) {
    if (!execSql(db, "BEGIN IMMEDIATE;")) return false;

    auto rollback = [&]() noexcept {
        (void)execSql(db, "ROLLBACK;");
        return false;
    };

    if (!ensureCourse(db, task.courseId)) return rollback();

    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) return rollback();
    StmtPtr stmt(raw);

    const std::string depsJson = nlohmann::json(task.dependencies).dump();
    bindText(stmt.get(), 1, task.id);
    if (task.courseId.empty()) sqlite3_bind_null(stmt.get(), 2);
    else bindText(stmt.get(), 2, task.courseId);
    bindText(stmt.get(), 3, task.title);
    sqlite3_bind_int(stmt.get(), 4, priorityToSql(task.priority));
    bindText(stmt.get(), 5, statusToSql(task.status));
    sqlite3_bind_int(stmt.get(), 6, task.estimatedMinutes);
    sqlite3_bind_int(stmt.get(), 7, task.remainingMinutes);
    if (task.deadline.has_value()) {
        const std::string ticks = std::to_string(task.deadline->time_since_epoch().count());
        bindText(stmt.get(), 8, ticks);
    } else {
        sqlite3_bind_null(stmt.get(), 8);
    }
    bindText(stmt.get(), 9, depsJson);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return rollback();
    if (requireChanged && sqlite3_changes(db) <= 0) return rollback();

    return execSql(db, "COMMIT;") ? true : rollback();
}

std::vector<Task> queryTasks(sqlite3* db, const char* suffixSql,
                             std::optional<std::string_view> arg = std::nullopt) {
    std::vector<Task> result;
    if (db == nullptr) return result;

    const std::string sql = std::string(
        "SELECT task_id, course_id, title, priority, status, "
        "estimated_duration, remaining_duration, deadline_utc, dependencies_json FROM tasks "
    ) + suffixSql;

    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) return result;
    StmtPtr stmt(raw);

    if (arg.has_value() && bindText(stmt.get(), 1, *arg) != SQLITE_OK) return result;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        Task t;
        t.id = colText(stmt.get(), 0);
        t.courseId = colText(stmt.get(), 1);
        t.title = colText(stmt.get(), 2);
        t.priority = priorityFromSql(sqlite3_column_int(stmt.get(), 3));
        t.status = statusFromSql(colText(stmt.get(), 4));
        t.estimatedMinutes = sqlite3_column_int(stmt.get(), 5);
        t.remainingMinutes = sqlite3_column_int(stmt.get(), 6);

        if (sqlite3_column_type(stmt.get(), 7) != SQLITE_NULL) {
            const std::string rawTicks = colText(stmt.get(), 7);
            long long ticks = 0;
            const auto [ptr, ec] = std::from_chars(rawTicks.data(), rawTicks.data() + rawTicks.size(), ticks);
            if (ec == std::errc{} && ptr == rawTicks.data() + rawTicks.size()) {
                t.deadline = TimePoint{TimePoint::duration{ticks}};
            }
        }

        const auto j = nlohmann::json::parse(colText(stmt.get(), 8), nullptr, false);
        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.is_string()) t.dependencies.push_back(item.get<std::string>());
            }
        }
        result.push_back(std::move(t));
    }
    return result;
}

} // namespace

void SqliteTaskRepository::DbDeleter::operator()(sqlite3* db) const noexcept {
    if (db != nullptr) sqlite3_close_v2(db);
}

SqliteTaskRepository::SqliteTaskRepository(std::filesystem::path dbPath)
    : dbPath_(std::move(dbPath)) {
    const std::string pathStr = dbPath_.string();
    if (pathStr != ":memory:" && dbPath_.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(dbPath_.parent_path(), ec);
    }

    sqlite3* raw = nullptr;
    if (sqlite3_open(pathStr.c_str(), &raw) != SQLITE_OK) {
        if (raw != nullptr) sqlite3_close_v2(raw);
        return;
    }

    db_.reset(raw);
    sqlite3_busy_timeout(db_.get(), 5000);
    if (!execSql(db_.get(), "PRAGMA foreign_keys = ON;") || !initSchema(db_.get())) {
        db_.reset();
    }
}

SqliteTaskRepository::~SqliteTaskRepository() = default;
SqliteTaskRepository::SqliteTaskRepository(SqliteTaskRepository&&) noexcept = default;
SqliteTaskRepository& SqliteTaskRepository::operator=(SqliteTaskRepository&&) noexcept = default;

bool SqliteTaskRepository::add(const Task& task) {
    if (!db_ || !task.isValid() || exists(task.id)) return false;
    const char* sql =
        "INSERT INTO tasks (task_id, course_id, title, priority, status, "
        "estimated_duration, remaining_duration, deadline_utc, dependencies_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);";
    return executeWrite(db_.get(), sql, task, false);
}

std::optional<Task> SqliteTaskRepository::findById(std::string_view id) const {
    if (id.empty()) return std::nullopt;
    auto rows = queryTasks(db_.get(), "WHERE task_id = ?1 LIMIT 1;", id);
    if (rows.empty()) return std::nullopt;
    return std::move(rows.front());
}

std::vector<Task> SqliteTaskRepository::findAll() const {
    return queryTasks(db_.get(), "ORDER BY rowid ASC;");
}

std::vector<Task> SqliteTaskRepository::findByCourse(std::string_view courseId) const {
    return queryTasks(db_.get(), "WHERE COALESCE(course_id, '') = ?1 ORDER BY rowid ASC;", courseId);
}

std::vector<Task> SqliteTaskRepository::findByStatus(TaskStatus status) const {
    return queryTasks(db_.get(), "WHERE status = ?1 ORDER BY rowid ASC;", statusToSql(status));
}

bool SqliteTaskRepository::update(const Task& task) {
    if (!db_ || !task.isValid() || !exists(task.id)) return false;
    const char* sql =
        "UPDATE tasks SET course_id = ?2, title = ?3, priority = ?4, status = ?5, "
        "estimated_duration = ?6, remaining_duration = ?7, deadline_utc = ?8, "
        "dependencies_json = ?9, updated_at_utc = strftime('%Y-%m-%dT%H:%M:%SZ','now') "
        "WHERE task_id = ?1;";
    return executeWrite(db_.get(), sql, task, true);
}

bool SqliteTaskRepository::remove(std::string_view id) {
    if (!db_ || id.empty()) return false;
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db_.get(), "DELETE FROM tasks WHERE task_id = ?1;", -1, &raw, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtPtr stmt(raw);
    bindText(stmt.get(), 1, id);
    return sqlite3_step(stmt.get()) == SQLITE_DONE && sqlite3_changes(db_.get()) > 0;
}

bool SqliteTaskRepository::exists(std::string_view id) const noexcept {
    if (!db_ || id.empty()) return false;
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db_.get(), "SELECT 1 FROM tasks WHERE task_id = ?1 LIMIT 1;", -1, &raw, nullptr) != SQLITE_OK) {
        return false;
    }
    StmtPtr stmt(raw);
    sqlite3_bind_text(stmt.get(), 1, id.data(), static_cast<int>(id.size()), SQLITE_STATIC);
    return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

std::size_t SqliteTaskRepository::count() const noexcept {
    if (!db_) return 0;
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db_.get(), "SELECT COUNT(*) FROM tasks;", -1, &raw, nullptr) != SQLITE_OK) {
        return 0;
    }
    StmtPtr stmt(raw);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return 0;
    const sqlite3_int64 n = sqlite3_column_int64(stmt.get(), 0);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

void SqliteTaskRepository::clear() noexcept {
    (void)execSql(db_.get(), "DELETE FROM tasks;");
}

} // namespace edu_schedule
