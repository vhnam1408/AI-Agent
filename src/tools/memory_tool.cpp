#include "memory_tool.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace edu_schedule {

namespace {

// Deleter tự động đóng kết nối SQLite khi con trỏ unique_ptr ra khỏi phạm vi (RAII)
struct SqliteDbDeleter {
    void operator()(sqlite3* db) const noexcept {
        if (db != nullptr) {
            sqlite3_close_v2(db);
        }
    }
};

// Deleter tự động giải phóng câu lệnh Prepared Statement (RAII)
struct SqliteStmtDeleter {
    void operator()(sqlite3_stmt* stmt) const noexcept {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
    }
};

using SqliteDbPtr = std::unique_ptr<sqlite3, SqliteDbDeleter>;
using SqliteStmtPtr = std::unique_ptr<sqlite3_stmt, SqliteStmtDeleter>;

// Hàm tiện ích: Cắt bỏ khoảng trắng thừa ở hai đầu chuỗi
std::string trim(std::string_view s) {
    const auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    const auto end = s.find_last_not_of(" \t\n\r");
    return std::string(s.substr(start, end - start + 1));
}

// Hàm tiện ích: Tạo mã memory_id tự động dựa trên thời gian thực (mili-giây)
std::string generateMemoryId() {
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return "mem_" + std::to_string(nowMs);
}

// Hàm tiện ích: Mở kết nối tới file SQLite và bật ràng buộc khóa ngoại
SqliteDbPtr openDatabase(const std::filesystem::path& dbPath, std::string* errorMessage = nullptr) {
    if (dbPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(dbPath.parent_path(), ec);
    }

    sqlite3* rawDb = nullptr;
    const std::string pathStr = dbPath.string();
    if (sqlite3_open(pathStr.c_str(), &rawDb) != SQLITE_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = rawDb ? sqlite3_errmsg(rawDb) : "Cannot open SQLite database";
        }
        if (rawDb != nullptr) sqlite3_close_v2(rawDb);
        return nullptr;
    }

    SqliteDbPtr db(rawDb);

    // Quy tắc bắt buộc từ TASK-04: Bật khóa ngoại trên mỗi kết nối
    char* errMsg = nullptr;
    if (sqlite3_exec(db.get(), "PRAGMA foreign_keys = ON;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errorMessage != nullptr && errMsg != nullptr) {
            *errorMessage = errMsg;
        }
        sqlite3_free(errMsg);
        return nullptr;
    }

    return db;
}

// Schema SQL chuẩn từ tài liệu thiết kế TASK-04 của Ân
const char* kSchemaSql = R"SQL(
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
    deadline_utc        TEXT,
    created_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at_utc      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    FOREIGN KEY (course_id) REFERENCES courses(course_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS task_dependencies (
    parent_task_id TEXT NOT NULL,
    child_task_id  TEXT NOT NULL,
    PRIMARY KEY (parent_task_id, child_task_id),
    CHECK (parent_task_id <> child_task_id),
    FOREIGN KEY (parent_task_id) REFERENCES tasks(task_id) ON DELETE CASCADE,
    FOREIGN KEY (child_task_id) REFERENCES tasks(task_id) ON DELETE CASCADE
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
    FOREIGN KEY (memory_id) REFERENCES memories(memory_id) ON DELETE CASCADE
);
)SQL";

} // namespace

// ============================================================================
// Hiện thực MemoryToolBase
// ============================================================================

MemoryToolBase::MemoryToolBase(std::filesystem::path dbPath)
    : dbPath_(std::move(dbPath)) {
    (void)initializeDatabase();
}

bool MemoryToolBase::initializeDatabase(std::string* errorMessage) {
    SqliteDbPtr db = openDatabase(dbPath_, errorMessage);
    if (!db) return false;

    char* errMsg = nullptr;
    if (sqlite3_exec(db.get(), kSchemaSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errorMessage != nullptr && errMsg != nullptr) {
            *errorMessage = errMsg;
        }
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool MemoryToolBase::hasTable(std::string_view tableName) const {
    SqliteDbPtr db = openDatabase(dbPath_);
    if (!db) return false;

    const char* sql = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1;";
    sqlite3_stmt* rawStmt = nullptr;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
        return false;
    }
    SqliteStmtPtr stmt(rawStmt);

    sqlite3_bind_text(stmt.get(), 1, tableName.data(), static_cast<int>(tableName.size()), SQLITE_TRANSIENT);
    return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

// ============================================================================
// Hiện thực MemorySaveTool ("memory_save")
// ============================================================================

MemorySaveTool::MemorySaveTool(std::filesystem::path dbPath)
    : MemoryToolBase(std::move(dbPath)) {}

std::string_view MemorySaveTool::name() const noexcept {
    return "memory_save";
}

std::string_view MemorySaveTool::description() const noexcept {
    return "Save a study note or memory into SQLite. Input: plain text or JSON {\"content\": \"...\", \"memory_id\": \"...\", \"source_type\": \"...\", \"source_id\": \"...\"}.";
}

ToolResult MemorySaveTool::execute(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return {false, "Error: Memory content cannot be empty."};
    }

    std::string memoryId;
    std::string content = trimmed;
    std::string sourceType = "note";
    std::string sourceId;

    // Nếu đầu vào là JSON thì bóc tách các trường tương ứng
    if (trimmed.starts_with('{')) {
        try {
            const auto j = nlohmann::json::parse(trimmed);
            content = trim(j.value("content", ""));
            memoryId = trim(j.value("memory_id", ""));
            sourceType = trim(j.value("source_type", "note"));
            sourceId = trim(j.value("source_id", ""));
        } catch (const std::exception& e) {
            return {false, std::string("Error parsing JSON input: ") + e.what()};
        }
    }

    if (content.empty()) {
        return {false, "Error: JSON field 'content' cannot be empty."};
    }
    if (memoryId.empty()) {
        memoryId = generateMemoryId();
    }

    std::string dbError;
    SqliteDbPtr db = openDatabase(dbPath_, &dbError);
    if (!db) {
        return {false, "Error opening SQLite database: " + dbError};
    }

    // Dùng Prepared Statement để lưu dữ liệu an toàn, chống SQL Injection
    const char* sql = R"SQL(
        INSERT INTO memories (memory_id, content, source_type, source_id)
        VALUES (?1, ?2, ?3, ?4)
        ON CONFLICT(memory_id) DO UPDATE SET
            content = excluded.content,
            source_type = excluded.source_type,
            source_id = excluded.source_id;
    )SQL";

    sqlite3_stmt* rawStmt = nullptr;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
        return {false, std::string("Error preparing SQL statement: ") + sqlite3_errmsg(db.get())};
    }
    SqliteStmtPtr stmt(rawStmt);

    sqlite3_bind_text(stmt.get(), 1, memoryId.c_str(), static_cast<int>(memoryId.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, content.c_str(), static_cast<int>(content.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, sourceType.c_str(), static_cast<int>(sourceType.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, sourceId.c_str(), static_cast<int>(sourceId.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return {false, std::string("Error saving memory: ") + sqlite3_errmsg(db.get())};
    }

    nlohmann::json resultJson = {
        {"saved", true},
        {"memory_id", memoryId},
        {"storage", "sqlite"}
    };
    return {true, resultJson.dump()};
}

// ============================================================================
// Hiện thực MemorySearchTool ("memory_search")
// ============================================================================

MemorySearchTool::MemorySearchTool(std::filesystem::path dbPath)
    : MemoryToolBase(std::move(dbPath)) {}

std::string_view MemorySearchTool::name() const noexcept {
    return "memory_search";
}

std::string_view MemorySearchTool::description() const noexcept {
    return "Search saved memories in SQLite by keyword. Input: keyword string or JSON {\"query\": \"...\", \"limit\": 5}.";
}

ToolResult MemorySearchTool::execute(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return {false, "Error: Search query cannot be empty."};
    }

    std::string query = trimmed;
    int limit = 10;

    // Nếu đầu vào là JSON thì đọc trường "query" và "limit"
    if (trimmed.starts_with('{')) {
        try {
            const auto j = nlohmann::json::parse(trimmed);
            query = trim(j.value("query", ""));
            limit = j.value("limit", 10);
        } catch (const std::exception& e) {
            return {false, std::string("Error parsing JSON input: ") + e.what()};
        }
    }

    if (query.empty() || limit <= 0) {
        return {false, "Error: Search 'query' must be non-empty and 'limit' must be positive."};
    }

    std::string dbError;
    SqliteDbPtr db = openDatabase(dbPath_, &dbError);
    if (!db) {
        return {false, "Error opening SQLite database: " + dbError};
    }

    const char* sql = R"SQL(
        SELECT memory_id, content, COALESCE(source_type, ''), COALESCE(source_id, ''), created_at_utc
        FROM memories
        WHERE content LIKE ?1 OR source_id LIKE ?1
        ORDER BY created_at_utc DESC, rowid DESC
        LIMIT ?2;
    )SQL";

    sqlite3_stmt* rawStmt = nullptr;
    if (sqlite3_prepare_v2(db.get(), sql, -1, &rawStmt, nullptr) != SQLITE_OK) {
        return {false, std::string("Error preparing SQL statement: ") + sqlite3_errmsg(db.get())};
    }
    SqliteStmtPtr stmt(rawStmt);

    const std::string likePattern = "%" + query + "%";
    sqlite3_bind_text(stmt.get(), 1, likePattern.c_str(), static_cast<int>(likePattern.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, limit);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const auto getColText = [&](int col) -> std::string {
            const unsigned char* text = sqlite3_column_text(stmt.get(), col);
            return text ? reinterpret_cast<const char*>(text) : "";
        };

        items.push_back({
            {"memory_id", getColText(0)},
            {"content", getColText(1)},
            {"source_type", getColText(2)},
            {"source_id", getColText(3)},
            {"created_at_utc", getColText(4)}
        });
    }

    nlohmann::json resultJson = {
        {"query", query},
        {"count", items.size()},
        {"results", std::move(items)}
    };
    return {true, resultJson.dump()};
}

} // namespace edu_schedule
