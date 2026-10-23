#include "memory_tool.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace edu_schedule {
namespace {

constexpr std::size_t kMaxInputLength = 16384;
constexpr int kDefaultSearchLimit = 10;
constexpr int kMaxSearchLimit = 100;

struct SqliteDbDeleter {
    void operator()(sqlite3* db) const noexcept {
        if (db != nullptr) {
            sqlite3_close_v2(db);
        }
    }
};

struct SqliteStmtDeleter {
    void operator()(sqlite3_stmt* stmt) const noexcept {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
    }
};

using SqliteDbPtr = std::unique_ptr<sqlite3, SqliteDbDeleter>;
using SqliteStmtPtr = std::unique_ptr<sqlite3_stmt, SqliteStmtDeleter>;

std::string trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() &&
           std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }

    std::size_t last = text.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }

    return std::string(text.substr(first, last - first));
}

std::string toLower(std::string_view text) {
    std::string result(text);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return result;
}

std::string generateMemoryId() {
    static std::atomic<unsigned long long> counter{0};
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const auto seq = ++counter;
    return "mem_" + std::to_string(nowMs) + "_" + std::to_string(seq);
}

bool executeSql(sqlite3* db, const char* sql, std::string* errorMessage = nullptr) {
    char* rawErrMsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &rawErrMsg);
    if (rc != SQLITE_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = rawErrMsg != nullptr ? rawErrMsg : sqlite3_errmsg(db);
        }
        if (rawErrMsg != nullptr) {
            sqlite3_free(rawErrMsg);
        }
        return false;
    }
    return true;
}

SqliteDbPtr openConnection(
    const std::filesystem::path& dbPath,
    std::string* errorMessage = nullptr
) {
    if (dbPath.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Database path cannot be empty";
        }
        return nullptr;
    }

    if (dbPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(dbPath.parent_path(), ec);
        if (ec) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to create database directory: " + ec.message();
            }
            return nullptr;
        }
    }

    sqlite3* rawDb = nullptr;
    const std::string pathUtf8 = dbPath.string();
    const int rc = sqlite3_open_v2(
        pathUtf8.c_str(),
        &rawDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );

    SqliteDbPtr db(rawDb);
    if (rc != SQLITE_OK || !db) {
        if (errorMessage != nullptr) {
            *errorMessage = db ? sqlite3_errmsg(db.get()) : "Unable to allocate sqlite3 handle";
        }
        return nullptr;
    }

    sqlite3_busy_timeout(db.get(), 3000);

    // Quy tắc bắt buộc từ TASK-04: Bật khóa ngoại trên từng connection.
    if (!executeSql(db.get(), "PRAGMA foreign_keys = ON;", errorMessage)) {
        return nullptr;
    }

    return db;
}

const char* kSchemaSql = R"SQL(
BEGIN;

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
    priority            INTEGER NOT NULL DEFAULT 3
                        CHECK (priority BETWEEN 1 AND 5),
    status              TEXT NOT NULL DEFAULT 'To-do'
                        CHECK (status IN (
                            'To-do', 'In-Progress', 'Done',
                            'Cancelled', 'Blocked'
                        )),
    estimated_duration  INTEGER CHECK (
                            estimated_duration IS NULL OR estimated_duration >= 0
                        ),
    actual_duration     INTEGER CHECK (
                            actual_duration IS NULL OR actual_duration >= 0
                        ),
    deadline_utc        TEXT,
    created_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    FOREIGN KEY (course_id) REFERENCES courses(course_id)
        ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS task_dependencies (
    parent_task_id TEXT NOT NULL,
    child_task_id  TEXT NOT NULL,
    PRIMARY KEY (parent_task_id, child_task_id),
    CHECK (parent_task_id <> child_task_id),
    FOREIGN KEY (parent_task_id) REFERENCES tasks(task_id)
        ON DELETE CASCADE,
    FOREIGN KEY (child_task_id) REFERENCES tasks(task_id)
        ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS memories (
    memory_id      TEXT PRIMARY KEY,
    content        TEXT NOT NULL,
    source_type    TEXT,
    source_id      TEXT,
    created_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS memory_embeddings (
    id              INTEGER PRIMARY KEY,
    memory_id       TEXT NOT NULL UNIQUE,
    model_name      TEXT NOT NULL,
    dimensions      INTEGER NOT NULL CHECK (dimensions > 0),
    embedding       BLOB NOT NULL,
    embedded_at_utc TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    FOREIGN KEY (memory_id) REFERENCES memories(memory_id)
        ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_tasks_course_id
    ON tasks(course_id);

CREATE INDEX IF NOT EXISTS idx_tasks_deadline_utc
    ON tasks(deadline_utc);

CREATE INDEX IF NOT EXISTS idx_task_dependencies_child
    ON task_dependencies(child_task_id);

CREATE INDEX IF NOT EXISTS idx_memory_embeddings_model
    ON memory_embeddings(model_name, dimensions);

COMMIT;
)SQL";

std::string columnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* raw = sqlite3_column_text(stmt, col);
    if (raw == nullptr) {
        return {};
    }
    return reinterpret_cast<const char*>(raw);
}

bool bindOptionalText(sqlite3_stmt* stmt, int index, std::string_view value) {
    if (value.empty()) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK;
    }
    return sqlite3_bind_text(
        stmt,
        index,
        value.data(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    ) == SQLITE_OK;
}

ToolResult saveStructuredTaskAndMemory(
    sqlite3* db,
    const nlohmann::json& payload,
    std::string_view fallbackMemoryId,
    std::string_view contentText,
    std::string_view sourceType,
    std::string_view sourceId
) {
    std::string errMsg;
    if (!executeSql(db, "BEGIN;", &errMsg)) {
        return {false, "Failed to start transaction: " + errMsg};
    }

    const auto rollbackWithError = [&](const std::string& reason) -> ToolResult {
        (void)executeSql(db, "ROLLBACK;", nullptr);
        return {false, reason};
    };

    const std::string courseId = payload.value("course_id", "");
    const std::string courseName = payload.value("course_name", courseId);
    const std::string lecturer = payload.value("lecturer", "");

    if (!courseId.empty()) {
        const char* upsertCourseSql = R"SQL(
            INSERT INTO courses (course_id, course_name, lecturer)
            VALUES (?1, ?2, ?3)
            ON CONFLICT(course_id) DO UPDATE SET
                course_name = excluded.course_name,
                lecturer = COALESCE(excluded.lecturer, courses.lecturer);
        )SQL";
        sqlite3_stmt* rawCourseStmt = nullptr;
        if (sqlite3_prepare_v2(db, upsertCourseSql, -1, &rawCourseStmt, nullptr) != SQLITE_OK) {
            return rollbackWithError("Failed to prepare course upsert: " + std::string(sqlite3_errmsg(db)));
        }
        SqliteStmtPtr courseStmt(rawCourseStmt);

        const std::string resolvedCourseName = courseName.empty() ? courseId : courseName;
        sqlite3_bind_text(courseStmt.get(), 1, courseId.c_str(), static_cast<int>(courseId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(courseStmt.get(), 2, resolvedCourseName.c_str(), static_cast<int>(resolvedCourseName.size()), SQLITE_TRANSIENT);
        bindOptionalText(courseStmt.get(), 3, lecturer);

        if (sqlite3_step(courseStmt.get()) != SQLITE_DONE) {
            return rollbackWithError("Failed to save course: " + std::string(sqlite3_errmsg(db)));
        }
    }

    const std::string taskId = payload.value("task_id", "");
    if (!taskId.empty()) {
        const std::string title = payload.value("title", contentText.empty() ? taskId : std::string(contentText));
        const std::string description = payload.value("description", std::string(contentText));
        const int priority = std::clamp(payload.value("priority", 3), 1, 5);
        const std::string status = payload.value("status", "To-do");
        const std::string deadlineUtc = payload.value("deadline_utc", "");

        const char* upsertTaskSql = R"SQL(
            INSERT INTO tasks (
                task_id, course_id, title, description, priority, status,
                estimated_duration, actual_duration, deadline_utc, updated_at_utc
            ) VALUES (
                ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, strftime('%Y-%m-%dT%H:%M:%SZ','now')
            )
            ON CONFLICT(task_id) DO UPDATE SET
                course_id = COALESCE(excluded.course_id, tasks.course_id),
                title = excluded.title,
                description = COALESCE(excluded.description, tasks.description),
                priority = excluded.priority,
                status = excluded.status,
                estimated_duration = COALESCE(excluded.estimated_duration, tasks.estimated_duration),
                actual_duration = COALESCE(excluded.actual_duration, tasks.actual_duration),
                deadline_utc = COALESCE(excluded.deadline_utc, tasks.deadline_utc),
                updated_at_utc = strftime('%Y-%m-%dT%H:%M:%SZ','now');
        )SQL";

        sqlite3_stmt* rawTaskStmt = nullptr;
        if (sqlite3_prepare_v2(db, upsertTaskSql, -1, &rawTaskStmt, nullptr) != SQLITE_OK) {
            return rollbackWithError("Failed to prepare task upsert: " + std::string(sqlite3_errmsg(db)));
        }
        SqliteStmtPtr taskStmt(rawTaskStmt);

        sqlite3_bind_text(taskStmt.get(), 1, taskId.c_str(), static_cast<int>(taskId.size()), SQLITE_TRANSIENT);
        bindOptionalText(taskStmt.get(), 2, courseId);
        sqlite3_bind_text(taskStmt.get(), 3, title.c_str(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
        bindOptionalText(taskStmt.get(), 4, description);
        sqlite3_bind_int(taskStmt.get(), 5, priority);
        sqlite3_bind_text(taskStmt.get(), 6, status.c_str(), static_cast<int>(status.size()), SQLITE_TRANSIENT);

        if (payload.contains("estimated_duration") && payload["estimated_duration"].is_number_integer()) {
            const int est = payload["estimated_duration"].get<int>();
            if (est < 0) {
                return rollbackWithError("estimated_duration must be non-negative");
            }
            sqlite3_bind_int(taskStmt.get(), 7, est);
        } else {
            sqlite3_bind_null(taskStmt.get(), 7);
        }

        if (payload.contains("actual_duration") && payload["actual_duration"].is_number_integer()) {
            const int act = payload["actual_duration"].get<int>();
            if (act < 0) {
                return rollbackWithError("actual_duration must be non-negative");
            }
            sqlite3_bind_int(taskStmt.get(), 8, act);
        } else {
            sqlite3_bind_null(taskStmt.get(), 8);
        }

        bindOptionalText(taskStmt.get(), 9, deadlineUtc);

        if (sqlite3_step(taskStmt.get()) != SQLITE_DONE) {
            return rollbackWithError("Failed to save task: " + std::string(sqlite3_errmsg(db)));
        }
    }

    const std::string resolvedMemoryId = !fallbackMemoryId.empty()
        ? std::string(fallbackMemoryId)
        : (!taskId.empty() ? "mem_" + taskId : generateMemoryId());

    const char* upsertMemorySql = R"SQL(
        INSERT INTO memories (memory_id, content, source_type, source_id)
        VALUES (?1, ?2, ?3, ?4)
        ON CONFLICT(memory_id) DO UPDATE SET
            content = excluded.content,
            source_type = excluded.source_type,
            source_id = excluded.source_id;
    )SQL";

    sqlite3_stmt* rawMemStmt = nullptr;
    if (sqlite3_prepare_v2(db, upsertMemorySql, -1, &rawMemStmt, nullptr) != SQLITE_OK) {
        return rollbackWithError("Failed to prepare memory upsert: " + std::string(sqlite3_errmsg(db)));
    }
    SqliteStmtPtr memStmt(rawMemStmt);

    sqlite3_bind_text(memStmt.get(), 1, resolvedMemoryId.c_str(), static_cast<int>(resolvedMemoryId.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(memStmt.get(), 2, contentText.data(), static_cast<int>(contentText.size()), SQLITE_TRANSIENT);
    bindOptionalText(memStmt.get(), 3, sourceType);
    bindOptionalText(memStmt.get(), 4, sourceId);

    if (sqlite3_step(memStmt.get()) != SQLITE_DONE) {
        return rollbackWithError("Failed to save memory: " + std::string(sqlite3_errmsg(db)));
    }

    if (!executeSql(db, "COMMIT;", &errMsg)) {
        return rollbackWithError("Failed to commit transaction: " + errMsg);
    }

    nlohmann::json response = {
        {"status", "success"},
        {"data", {
            {"saved", true},
            {"storage", "sqlite"},
            {"memory_id", resolvedMemoryId}
        }},
        {"error", nullptr}
    };
    if (!taskId.empty()) {
        response["data"]["task_id"] = taskId;
    }
    if (!courseId.empty()) {
        response["data"]["course_id"] = courseId;
    }

    return {true, response.dump()};
}

} // namespace

MemoryTool::MemoryTool(std::filesystem::path dbPath, Mode mode)
    : dbPath_(std::move(dbPath)), mode_(mode) {
    schemaInitialized_ = initializeSchema(&initError_);
}

std::string_view MemoryTool::name() const noexcept {
    switch (mode_) {
        case Mode::SaveOnly:
            return "memory_save";
        case Mode::SearchOnly:
            return "memory_search";
        case Mode::Unified:
        default:
            return "memory";
    }
}

std::string_view MemoryTool::description() const noexcept {
    switch (mode_) {
        case Mode::SaveOnly:
            return "Save study notes, task metadata, deadlines, or duration history into SQLite persistent memory.";
        case Mode::SearchOnly:
            return "Search saved notes, task metadata, and study history from SQLite persistent memory by keyword.";
        case Mode::Unified:
        default:
            return "Save and search persistent study memories in SQLite (supports memory_save and memory_search actions).";
    }
}

bool MemoryTool::initializeSchema(std::string* errorMessage) {
    std::string localError;
    SqliteDbPtr db = openConnection(dbPath_, &localError);
    if (!db) {
        if (errorMessage != nullptr) {
            *errorMessage = localError;
        }
        initError_ = localError;
        schemaInitialized_ = false;
        return false;
    }

    if (!executeSql(db.get(), kSchemaSql, &localError)) {
        (void)executeSql(db.get(), "ROLLBACK;", nullptr);
        if (errorMessage != nullptr) {
            *errorMessage = localError;
        }
        initError_ = localError;
        schemaInitialized_ = false;
        return false;
    }

    initError_.clear();
    schemaInitialized_ = true;
    return true;
}

bool MemoryTool::hasTable(std::string_view tableName) const {
    if (tableName.empty()) {
        return false;
    }

    SqliteDbPtr db = openConnection(dbPath_, nullptr);
    if (!db) {
        return false;
    }

    const char* sql =
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1;";
    sqlite3_stmt* rawStmt = nullptr;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
        return false;
    }
    SqliteStmtPtr stmt(rawStmt);

    sqlite3_bind_text(
        stmt.get(),
        1,
        tableName.data(),
        static_cast<int>(tableName.size()),
        SQLITE_TRANSIENT
    );

    return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

ToolResult MemoryTool::saveMemory(
    std::string_view content,
    std::string_view memoryId,
    std::string_view sourceType,
    std::string_view sourceId
) {
    const std::string trimmedContent = trim(content);
    if (trimmedContent.empty()) {
        return {false, "Memory content cannot be empty"};
    }

    if (!schemaInitialized_ && !initializeSchema(&initError_)) {
        return {false, "SQLite schema initialization failed: " + initError_};
    }

    std::string errMsg;
    SqliteDbPtr db = openConnection(dbPath_, &errMsg);
    if (!db) {
        return {false, "Failed to open SQLite database: " + errMsg};
    }

    return saveStructuredTaskAndMemory(
        db.get(),
        nlohmann::json::object(),
        trim(memoryId),
        trimmedContent,
        trim(sourceType),
        trim(sourceId)
    );
}

std::vector<MemoryEntry> MemoryTool::queryMemories(
    std::string_view query,
    int limit,
    std::string_view sourceType,
    std::string_view sourceId
) const {
    std::vector<MemoryEntry> results;

    SqliteDbPtr db = openConnection(dbPath_, nullptr);
    if (!db) {
        return results;
    }

    const int clampedLimit = std::clamp(
        limit > 0 ? limit : kDefaultSearchLimit,
        1,
        kMaxSearchLimit
    );

    const std::string trimmedQuery = trim(query);
    const std::string pattern = "%" + trimmedQuery + "%";
    const std::string trimmedType = trim(sourceType);
    const std::string trimmedSourceId = trim(sourceId);

    const char* sql = R"SQL(
        SELECT memory_id, content, COALESCE(source_type, ''), COALESCE(source_id, ''), created_at_utc
        FROM memories
        WHERE (?1 = '' OR content LIKE ?2 OR memory_id LIKE ?2 OR source_type LIKE ?2 OR source_id LIKE ?2)
          AND (?3 = '' OR source_type = ?3)
          AND (?4 = '' OR source_id = ?4)
        ORDER BY created_at_utc DESC, rowid DESC
        LIMIT ?5;
    )SQL";

    sqlite3_stmt* rawStmt = nullptr;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
        return results;
    }
    SqliteStmtPtr stmt(rawStmt);

    sqlite3_bind_text(stmt.get(), 1, trimmedQuery.c_str(), static_cast<int>(trimmedQuery.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, pattern.c_str(), static_cast<int>(pattern.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, trimmedType.c_str(), static_cast<int>(trimmedType.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, trimmedSourceId.c_str(), static_cast<int>(trimmedSourceId.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 5, clampedLimit);

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        MemoryEntry entry;
        entry.memory_id = columnText(stmt.get(), 0);
        entry.content = columnText(stmt.get(), 1);
        entry.source_type = columnText(stmt.get(), 2);
        entry.source_id = columnText(stmt.get(), 3);
        entry.created_at_utc = columnText(stmt.get(), 4);
        results.push_back(std::move(entry));
    }

    return results;
}

ToolResult MemoryTool::searchMemory(
    std::string_view query,
    int limit,
    std::string_view sourceType,
    std::string_view sourceId
) const {
    if (!schemaInitialized_) {
        return {false, "SQLite schema is not initialized: " + initError_};
    }

    std::string errMsg;
    SqliteDbPtr db = openConnection(dbPath_, &errMsg);
    if (!db) {
        return {false, "Failed to open SQLite database: " + errMsg};
    }

    const std::vector<MemoryEntry> entries =
        queryMemories(query, limit, sourceType, sourceId);

    nlohmann::json items = nlohmann::json::array();
    for (const auto& entry : entries) {
        items.push_back({
            {"memory_id", entry.memory_id},
            {"content", entry.content},
            {"source_type", entry.source_type},
            {"source_id", entry.source_id},
            {"created_at_utc", entry.created_at_utc}
        });
    }

    nlohmann::json response = {
        {"status", "success"},
        {"data", {
            {"query", trim(query)},
            {"count", entries.size()},
            {"results", std::move(items)}
        }},
        {"error", nullptr}
    };

    return {true, response.dump()};
}

ToolResult MemoryTool::execute(std::string_view input) {
    if (input.size() > kMaxInputLength) {
        return {false, "MemoryTool input exceeds maximum allowed length"};
    }

    const std::string trimmedInput = trim(input);
    if (trimmedInput.empty()) {
        return {false, "MemoryTool input cannot be empty"};
    }

    if (!schemaInitialized_ && !initializeSchema(&initError_)) {
        return {false, "SQLite schema initialization failed: " + initError_};
    }

    // 1. Xử lý đầu vào dạng JSON
    if (trimmedInput.front() == '{') {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(trimmedInput);
        } catch (const nlohmann::json::exception& ex) {
            return {false, std::string("Invalid JSON input: ") + ex.what()};
        }

        if (!payload.is_object()) {
            return {false, "MemoryTool JSON input must be an object"};
        }

        std::string action = toLower(trim(payload.value("action", "")));
        if (action.empty()) {
            if (mode_ == Mode::SaveOnly) {
                action = "memory_save";
            } else if (mode_ == Mode::SearchOnly) {
                action = "memory_search";
            } else if (payload.contains("query") || payload.contains("keyword")) {
                action = "memory_search";
            } else if (payload.contains("content") ||
                       payload.contains("text") ||
                       payload.contains("note") ||
                       payload.contains("task_id") ||
                       payload.contains("course_id")) {
                action = "memory_save";
            }
        }

        if (action == "save" || action == "memory_save") {
            if (mode_ == Mode::SearchOnly) {
                return {false, "memory_search tool does not accept save actions"};
            }

            std::string content = payload.value("content", "");
            if (content.empty()) {
                content = payload.value("text", "");
            }
            if (content.empty()) {
                content = payload.value("note", "");
            }
            if (content.empty()) {
                content = payload.value("title", "");
            }

            // Hỗ trợ định dạng args {course_id, task_id, deadline_utc} trong Trajectory JSON mẫu (TASK-04)
            if (content.empty() && (payload.contains("task_id") || payload.contains("course_id"))) {
                std::ostringstream summary;
                if (payload.contains("course_id")) {
                    summary << "course=" << payload.value("course_id", "") << " ";
                }
                if (payload.contains("task_id")) {
                    summary << "task=" << payload.value("task_id", "") << " ";
                }
                if (payload.contains("deadline_utc")) {
                    summary << "deadline=" << payload.value("deadline_utc", "") << " ";
                }
                if (payload.contains("estimated_duration") && payload["estimated_duration"].is_number()) {
                    summary << "estimated_mins=" << payload["estimated_duration"].dump() << " ";
                }
                if (payload.contains("actual_duration") && payload["actual_duration"].is_number()) {
                    summary << "actual_mins=" << payload["actual_duration"].dump() << " ";
                }
                content = trim(summary.str());
            }

            if (trim(content).empty()) {
                return {false, "memory_save requires non-empty 'content' or task fields"};
            }

            const std::string memoryId = payload.value("memory_id", payload.value("id", ""));
            std::string sourceType = payload.value("source_type", "");
            if (sourceType.empty()) {
                sourceType = payload.contains("task_id") ? "task" : "note";
            }
            std::string sourceId = payload.value("source_id", "");
            if (sourceId.empty()) {
                sourceId = payload.value("course_id", payload.value("task_id", ""));
            }

            std::string errMsg;
            SqliteDbPtr db = openConnection(dbPath_, &errMsg);
            if (!db) {
                return {false, "Failed to open SQLite database: " + errMsg};
            }

            return saveStructuredTaskAndMemory(
                db.get(),
                payload,
                trim(memoryId),
                trim(content),
                trim(sourceType),
                trim(sourceId)
            );
        }

        if (action == "search" || action == "memory_search") {
            if (mode_ == Mode::SaveOnly) {
                return {false, "memory_save tool does not accept search actions"};
            }

            std::string query = payload.value("query", "");
            if (query.empty()) {
                query = payload.value("keyword", "");
            }

            int limit = kDefaultSearchLimit;
            if (payload.contains("limit") && payload["limit"].is_number_integer()) {
                limit = payload["limit"].get<int>();
                if (limit <= 0) {
                    return {false, "Search 'limit' must be a positive integer"};
                }
            }

            const std::string sourceType = payload.value("source_type", "");
            const std::string sourceId = payload.value("source_id", payload.value("course_id", ""));
            return searchMemory(query, limit, sourceType, sourceId);
        }

        return {false, "Unknown or missing MemoryTool action in JSON input"};
    }

    // 2. Xử lý đầu vào dạng chuỗi lệnh ("memory_save: ...", "memory_search: ...", "save: ...", "search: ...")
    const std::size_t colonPos = trimmedInput.find(':');
    if (colonPos != std::string::npos) {
        const std::string prefix = toLower(trim(std::string_view(trimmedInput).substr(0, colonPos)));
        const std::string rest = trim(std::string_view(trimmedInput).substr(colonPos + 1));

        if (prefix == "save" || prefix == "memory_save") {
            if (mode_ == Mode::SearchOnly) {
                return {false, "memory_search tool does not accept save commands"};
            }
            return saveMemory(rest);
        }

        if (prefix == "search" || prefix == "memory_search") {
            if (mode_ == Mode::SaveOnly) {
                return {false, "memory_save tool does not accept search commands"};
            }
            return searchMemory(rest);
        }
    }

    // 3. Xử lý chuỗi thuần túy theo Mode của Tool
    if (mode_ == Mode::SaveOnly) {
        return saveMemory(trimmedInput);
    }
    if (mode_ == Mode::SearchOnly) {
        return searchMemory(trimmedInput);
    }

    return {
        false,
        "Ambiguous plain-text input for unified MemoryTool; use JSON or 'memory_save: ...' / 'memory_search: ...' prefix"
    };
}

} // namespace edu_schedule

