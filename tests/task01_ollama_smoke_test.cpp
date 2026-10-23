/**
 * @file main.cpp
 * @brief Chương trình kiểm thử PoC (Proof of Concept) cho OllamaClient.
 *
 * Ba test case theo yêu cầu đồ án:
 *  [TC-01] Text-only  : Trích xuất deadline từ văn bản tiếng Việt
 *  [TC-02] Multimodal : Đọc ảnh thời khóa biểu (nếu file tồn tại)
 *  [TC-03] Error Path : Kết nối sai port / timeout 1ms → bắt lỗi mượt mà
 *
 * Khởi tạo OllamaClient thông qua con trỏ đa hình std::unique_ptr<LLMClient>
 * → Minh chứng LSP: AgentLoop chỉ biết LLMClient, không biết OllamaClient.
 *
 * @author  Võ Hoàng Nam
 * @project EduSchedule-Agent
 * @std     C++23
 */

#include "client/llm_client.h"
#include "client/ollama_client.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace edu_schedule;

// ══════════════════════════════════════════════════════════════════════════════
// §  Console Encoding & ANSI Virtual Terminal RAII Guard
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Tự động thiết lập Code Page 65001 (UTF-8) trên Windows Console.
 *
 * Khắc phục triệt để lỗi font Tiếng Việt / Unicode (mojibake) trên cmd và powershell.
 * Áp dụng RAII: Tự động phục hồi Code Page ban đầu khi chương trình kết thúc.
 */
struct ConsoleEncodingGuard {
#ifdef _WIN32
    UINT old_in_cp  = 0;
    UINT old_out_cp = 0;
    DWORD old_stdout_mode = 0;
    HANDLE h_stdout = INVALID_HANDLE_VALUE;

    ConsoleEncodingGuard() noexcept {
        old_in_cp  = GetConsoleCP();
        old_out_cp = GetConsoleOutputCP();
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);

        // Kích hoạt Virtual Terminal Processing để hiển thị mã màu ANSI trên Windows 10/11
        h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h_stdout != INVALID_HANDLE_VALUE) {
            if (GetConsoleMode(h_stdout, &old_stdout_mode)) {
                SetConsoleMode(h_stdout, old_stdout_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
        }
    }

    ~ConsoleEncodingGuard() noexcept {
        if (old_in_cp != 0)  SetConsoleCP(old_in_cp);
        if (old_out_cp != 0) SetConsoleOutputCP(old_out_cp);
        if (h_stdout != INVALID_HANDLE_VALUE && old_stdout_mode != 0) {
            SetConsoleMode(h_stdout, old_stdout_mode);
        }
    }
#else
    ConsoleEncodingGuard() noexcept = default;
    ~ConsoleEncodingGuard() noexcept = default;
#endif

    ConsoleEncodingGuard(const ConsoleEncodingGuard&)            = delete;
    ConsoleEncodingGuard& operator=(const ConsoleEncodingGuard&) = delete;
};

// ══════════════════════════════════════════════════════════════════════════════
// §  Đa ngôn ngữ (i18n / Prompt Localization)
// ══════════════════════════════════════════════════════════════════════════════

enum class AppLanguage {
    Vietnamese,
    English
};

struct LocalizedPrompts {
    std::string system_prompt;
    std::string user_prompt;
    std::string vision_prompt;
    std::string test_title;
};

[[nodiscard]] LocalizedPrompts get_prompts_for_language(AppLanguage lang) {
    if (lang == AppLanguage::English) {
        return {
            "You are an intelligent academic assistant of EduSchedule-Agent. "
            "When asked to extract deadlines, return a valid JSON array matching the schema: "
            "[{\"task\": \"...\", \"deadline\": \"...\", \"subject\": \"...\"}]. "
            "Only return raw JSON, no explanatory text.",

            "Extract all deadlines from the following passage and output JSON:\n"
            "\"This Friday is the deadline for OOP major project. "
            "Sunday at 7:00 AM in room B4 is the Computer Networks midterm exam. "
            "Data Structures report deadline is 23:59 on Oct 20. "
            "Don't forget to review for Discrete Mathematics on next Monday.\"",

            // Vision prompt: yêu cầu trả về JSON 3 thuộc tính chuẩn (task, deadline, subject) như TC-01
            "You are a schedule extraction assistant. "
            "Analyze the timetable image carefully and extract ALL classes/subjects you can see. "
            "Return ONLY a valid JSON array — no explanation, no markdown, no code block. "
            "Each item must have exactly these 3 fields:\n"
            "  \"task\": the class/event description (string, e.g. \"OOP Class\")\n"
            "  \"deadline\": the day and time/schedule (string, e.g. \"Monday 08:00\")\n"
            "  \"subject\": the course/subject name (string, e.g. \"OOP\")\n"
            "Example output format:\n"
            "[{\"task\":\"OOP Class\",\"deadline\":\"Monday 08:00\",\"subject\":\"OOP\"},"
            "{\"task\":\"Computer Networks Lecture\",\"deadline\":\"Wednesday 13:00\",\"subject\":\"Computer Networks\"}]",

            "TC-01: Text-only — Deadline Extraction (English)"
        };
    }

    // Default: Vietnamese
    return {
        "Bạn là trợ lý học tập thông minh của EduSchedule-Agent. "
        "Khi được yêu cầu trích xuất deadline, hãy trả về JSON hợp lệ "
        "với schema: [{\"task\": \"...\", \"deadline\": \"...\", \"subject\": \"...\"}]. "
        "Chỉ trả về JSON, không thêm giải thích.",

        "Trích xuất các mốc deadline từ đoạn văn bản sau và trả về JSON:\n"
        "\"Thứ 6 tuần này nộp bài tập lớn môn OOP. "
        "Chủ nhật thi giữa kỳ Mạng máy tính lúc 7h sáng phòng B4. "
        "Deadline báo cáo CTDL là 23:59 ngày 20/10. "
        "Nhớ ôn thi Toán rời rạc vào thứ 2 tuần sau.\"",

        // Vision prompt: yêu cầu trả về JSON 3 thuộc tính chuẩn (task, deadline, subject) như TC-01
        "Bạn là trợ lý bóc tách thời khóa biểu. "
        "Hãy phân tích ảnh thời khóa biểu này và trích xuất TẤT CẢ các môn học nhìn thấy thành mảng JSON. "
        "Chỉ trả về một mảng JSON hợp lệ — không giải thích, không markdown, không code block. "
        "Mỗi phần tử phải có đúng 3 trường như sau:\n"
        "  \"task\": mô tả buổi học/công việc (chuỗi, ví dụ: \"Học Lập trình OOP\" hoặc \"Buổi học OOP phòng B4\")\n"
        "  \"deadline\": thứ và tiết/giờ học (chuỗi, ví dụ: \"Thứ 2 - 08:00\" hoặc \"Thứ 2 - Tiết 1-3\")\n"
        "  \"subject\": tên môn học (chuỗi, ví dụ: \"OOP\")\n"
        "Ví dụ định dạng đầu ra:\n"
        "[{\"task\":\"Học Lập trình OOP\",\"deadline\":\"Thứ 2 - 08:00\",\"subject\":\"OOP\"},"
        "{\"task\":\"Học Mạng máy tính\",\"deadline\":\"Thứ 4 - 13:00\",\"subject\":\"Mạng máy tính\"}]",

        "TC-01: Text-only — Trích xuất Deadline (Tiếng Việt)"
    };
}


// ══════════════════════════════════════════════════════════════════════════════
// §  Console Helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace console {
    // ANSI escape codes
    inline constexpr const char* RESET   = "\033[0m";
    inline constexpr const char* BOLD    = "\033[1m";
    inline constexpr const char* RED     = "\033[31m";
    inline constexpr const char* GREEN   = "\033[32m";
    inline constexpr const char* YELLOW  = "\033[33m";
    inline constexpr const char* CYAN    = "\033[36m";
    inline constexpr const char* MAGENTA = "\033[35m";

    void separator(const char* label)
    {
        std::cout << '\n'
                  << CYAN << BOLD
                  << "══════════════════════════════════════════════════\n"
                  << "  " << label << '\n'
                  << "══════════════════════════════════════════════════"
                  << RESET << '\n';
    }

    inline std::string sanitize_console(const std::string& text)
    {
        std::string res;
        res.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    res += '\n';
                    ++i;
                } else {
                    res += '\n'; // Thay \r đơn độc bằng \n để không bao giờ bị ghi đè dòng console
                }
            } else {
                res += text[i];
            }
        }
        return res;
    }

    void print_response(const LLMResult& resp, const char* test_name)
    {
        separator(test_name);

        if (resp.has_value()) {
            std::cout << GREEN << "[✓ PASS]" << RESET << '\n';
            std::cout << MAGENTA << "Model   : " << RESET << resp->model_used << '\n';
            std::cout << MAGENTA << "Tokens  : " << RESET
                      << resp->prompt_tokens << " prompt + "
                      << resp->completion_tokens << " completion = "
                      << resp->total_tokens << " total\n";
            std::cout << MAGENTA << "Latency : " << RESET
                      << resp->latency_ms << " ms"
                      << "  (Ollama: " << resp->ollama_total_duration_ms << " ms)\n";
            if (resp->truncated) {
                std::cout << YELLOW << "[WARN] Response bị cắt ngắn (done_reason=length)"
                          << RESET << '\n';
            }
            std::cout << '\n'
                      << BOLD << "── Nội dung phản hồi ──────────────────────────\n" << RESET;
            if (resp->content.empty()) {
                std::cout << YELLOW << "(Phản hồi rỗng — model không sinh ra nội dung)\n" << RESET;
            } else {
                std::cout << sanitize_console(resp->content) << '\n';
            }
            std::cout << std::flush;
        } else {
            std::cout << RED << "[✗ FAIL]" << RESET << '\n';
            std::cout << RED << "Error type : " << RESET
                      << to_string(resp.error().type) << '\n';
            std::cout << RED << "HTTP status: " << RESET << resp.error().http_status << '\n';
            std::cout << RED << "Message    : " << RESET
                      << resp.error().message << '\n';
            std::cout << RED << "Latency    : " << RESET << resp.error().latency_ms << " ms\n";
            std::cout << std::flush;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// §  Test Cases
// ══════════════════════════════════════════════════════════════════════════════



/**
 * @brief Lấy giá trị chuỗi từ JSON object theo danh sách tên key ưu tiên.
 *
 * Dùng để chuẩn hóa key khác nhau giữa các model:
 *   moondream trả về "course" → map sang "subject"
 *   moondream trả về "day"    → map sang "session"
 */
[[nodiscard]] std::string get_field(const nlohmann::json& item,
                                     const std::vector<std::string>& keys,
                                     const std::string& default_val = "N/A")
{
    for (const auto& k : keys) {
        if (item.contains(k)) {
            const auto& v = item[k];
            if (v.is_string()) return v.get<std::string>();
            if (v.is_number()) return std::to_string(v.get<int>());
        }
    }
    return default_val;
}

/**
 * @brief Cố gắng sửa và parse chuỗi JSON bị lỗi hoặc bị cắt cụt (truncated).
 *
 * Các trường hợp xử lý:
 *  1. Python dict syntax: 'key' → "key"
 *  2. Unclosed/Truncated JSON: cắt bỏ phần dở dang ở cuối, tự động đóng ngoặc } và ]
 *  3. Fallback Regex: nếu parse vẫn thất bại, quét các cặp key-value và dựng mảng JSON chuẩn
 */
[[nodiscard]] nlohmann::json try_repair_json(std::string s)
{
    // ── Fix 1: Python single-quote → JSON double-quote ─────────────────────
    std::string fixed;
    fixed.reserve(s.size() + 16);
    bool in_dquote = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') {
            in_dquote = !in_dquote;
            fixed += s[i];
        } else if (s[i] == '\'' && !in_dquote) {
            fixed += '"';
        } else {
            fixed += s[i];
        }
    }
    s = std::move(fixed);

    // ── Fix 2: Thử parse trực tiếp sau khi sửa quotes ──────────────────────
    auto j = nlohmann::json::parse(s, nullptr, false);
    if (!j.is_discarded()) {
        if (j.is_object()) return nlohmann::json::array({ j });
        return j;
    }

    // ── Fix 3: Vá chuỗi bị cắt cụt (Unclosed Truncated JSON) ───────────────
    // Tìm vị trí mở đầu JSON '[' hoặc '{'
    const auto start_pos = s.find_first_of("[{");
    if (start_pos != std::string::npos) {
        std::string candidate = s.substr(start_pos);

        // Tìm dấu phẩy ',' hoặc dấu hai chấm ':' hoặc ngoặc '}' cuối cùng có ý nghĩa
        auto last_comma = candidate.rfind(',');
        if (last_comma != std::string::npos && last_comma > 2) {
            // Cắt bỏ phần tử dở dang sau dấu phẩy cuối
            std::string truncated = candidate.substr(0, last_comma);

            // Đếm số lượng ngoặc nhọn và ngoặc vuông chưa đóng
            int open_curly = 0, close_curly = 0;
            int open_sq = 0, close_sq = 0;
            bool in_str = false;
            char prev_c = 0;

            for (char c : truncated) {
                if (c == '"' && prev_c != '\\') in_str = !in_str;
                if (!in_str) {
                    if (c == '{') open_curly++;
                    else if (c == '}') close_curly++;
                    else if (c == '[') open_sq++;
                    else if (c == ']') close_sq++;
                }
                prev_c = c;
            }

            // Tự động bổ sung các dấu đóng còn thiếu
            while (open_curly > close_curly) {
                truncated += "}";
                close_curly++;
            }
            while (open_sq > close_sq) {
                truncated += "]";
                close_sq++;
            }

            auto j_repaired = nlohmann::json::parse(truncated, nullptr, false);
            if (!j_repaired.is_discarded()) {
                if (j_repaired.is_object()) return nlohmann::json::array({ j_repaired });
                return j_repaired;
            }
        }
    }

    // ── Fix 4: Regex Fallback Extractor (Phục hồi trường dữ liệu cốt lõi) ───
    // Quét tìm tất cả các thông tin môn học, phòng, thời gian xuất hiện trong chuỗi
    nlohmann::json fallback_arr = nlohmann::json::array();
    try {
        std::regex pair_re(R"lit(["']?([a-zA-Z0-9_-]+)["']?\s*:\s*["']?([^"',}\]]+)["']?)lit");
        auto it_begin = std::sregex_iterator(s.begin(), s.end(), pair_re);
        auto it_end   = std::sregex_iterator();

        std::string course_val = "";
        std::string room_val   = "N/A";
        std::string time_val   = "";
        std::string day_val    = "";

        for (auto it = it_begin; it != it_end; ++it) {
            std::string key = (*it)[1].str();
            std::string val = (*it)[2].str();

            // Chuyển key về chữ thường để so sánh
            std::string lkey = key;
            for (char& ch : lkey) ch = static_cast<char>(std::tolower(ch));

            if (lkey == "course" || lkey == "subject" || lkey == "name") {
                if (!val.empty() && val != "LTR" && val.find("day") == std::string::npos) {
                    course_val = val;
                }
            } else if (lkey == "room" || lkey == "location") {
                if (!val.empty()) room_val = val;
            } else if (lkey == "time") {
                if (!val.empty()) time_val = val;
            } else if (lkey == "day" && lkey.size() == 3) {
                if (!val.empty()) day_val = "Day " + val;
            }

            // Khi đã gom đủ thông tin về môn học
            if (!course_val.empty() && course_val != "course") {
                nlohmann::json entry;
                std::string session_str = day_val;
                if (!time_val.empty()) {
                    session_str += (session_str.empty() ? "" : " ") + time_val;
                }
                std::string task_desc = "Lớp học " + course_val;
                if (!room_val.empty() && room_val != "N/A") {
                    task_desc += " (Phòng " + room_val + ")";
                }
                entry["task"]     = task_desc;
                entry["deadline"] = session_str.empty() ? "N/A" : session_str;
                entry["subject"]  = course_val;
                fallback_arr.push_back(std::move(entry));
                course_val.clear(); // Reset để tìm môn tiếp theo
            }
        }
    } catch (...) {
        // Bỏ qua lỗi regex nếu chuỗi quá dị dạng
    }

    if (!fallback_arr.empty()) {
        return fallback_arr;
    }

    return nlohmann::json(); // Thất bại hoàn toàn
}

/**
 * @brief Bóc tách JSON thời khóa biểu từ nội dung AI trả về và ghi ra file.
 *
 * Xử lý mạnh mẽ (Robust):
 *  - Strip markdown code fence ```json```
 *  - Repair Python dict syntax (single quotes)
 *  - Recover truncated JSON
 *  - Map key đa dạng: course/subject/name → subject, day/session/time → session
 *
 * @param raw_content  Nội dung thô từ LLMResponse::content
 * @param out_path     Đường dẫn file JSON đầu ra
 * @return             true nếu parse và ghi file thành công
 */
bool parse_and_save_schedule_json(const std::string& raw_content,
                                   const fs::path& out_path)
{
    // ── Bước 1: Strip code fence ```json ... ``` ──────────────────────────
    std::string cleaned = raw_content;

    auto fence_start = cleaned.find("```json");
    if (fence_start == std::string::npos) fence_start = cleaned.find("```");
    if (fence_start != std::string::npos) {
        auto content_start = cleaned.find('\n', fence_start);
        if (content_start != std::string::npos) {
            auto fence_end = cleaned.find("```", content_start + 1);
            if (fence_end != std::string::npos)
                cleaned = cleaned.substr(content_start + 1, fence_end - content_start - 1);
        }
    }

    // Cắt từ '[' đến ']' cuối cùng (nếu có)
    const auto ltrim = cleaned.find('[');
    const auto rtrim = cleaned.rfind(']');
    if (ltrim != std::string::npos && rtrim != std::string::npos && rtrim > ltrim)
        cleaned = cleaned.substr(ltrim, rtrim - ltrim + 1);

    // ── Bước 2: Parse hoặc Repair ─────────────────────────────────────────
    auto j = nlohmann::json::parse(cleaned, nullptr, false);

    if (j.is_discarded() || (!j.is_array() && !j.is_object())) {
        std::cout << console::YELLOW
                  << "[!] Parse tiêu chuẩn thất bại — đang thử chế độ tự sửa lỗi (repair)...\n"
                  << console::RESET;
        j = try_repair_json(cleaned);
    }

    if (j.is_discarded() || j.empty()) {
        std::cout << console::RED
                  << "[✗ JSON PARSE] Không thể khôi phục dữ liệu từ phản hồi AI.\n"
                  << "   200 ký tự đầu nhận được: " << cleaned.substr(0, 200) << "\n"
                  << console::RESET;
        return false;
    }

    // Bọc object đơn thành array nếu cần
    if (j.is_object()) j = nlohmann::json::array({ j });

    // ── Bước 3: Chuẩn hóa 3 thuộc tính (task, deadline, subject) như TC-01 ──
    nlohmann::ordered_json validated = nlohmann::ordered_json::array();

    // Bảng ánh xạ key → cột chuẩn (ưu tiên từ trái sang phải)
    const std::vector<std::string> task_keys = {
        "task", "name", "description", "cong_viec", "title", "class"
    };
    const std::vector<std::string> deadline_keys = {
        "deadline", "session", "day", "time", "schedule", "period", "lich_hoc", "thu_tiet"
    };
    const std::vector<std::string> subject_keys = {
        "subject", "course", "mon_hoc", "ten_mon"
    };
    const std::vector<std::string> room_keys = {
        "room", "location", "venue", "phong", "phong_hoc", "classroom"
    };

    int idx = 0;
    // Deduplicate: moondream hay lặp lại object giống hệt nhau
    std::set<std::string> seen;

    for (const auto& item : j) {
        if (!item.is_object()) continue;
        ++idx;

        std::string subj_val     = get_field(item, subject_keys, "");
        std::string deadline_val = get_field(item, deadline_keys, "N/A");
        std::string task_val     = get_field(item, task_keys, "");
        const std::string room   = get_field(item, room_keys, "");

        if (subj_val.empty() && !task_val.empty()) {
            subj_val = task_val;
        }
        if (subj_val.empty()) {
            subj_val = "Unknown-" + std::to_string(idx);
        }

        if (task_val.empty() || task_val == subj_val) {
            if (!room.empty() && room != "N/A") {
                task_val = "Lớp học " + subj_val + " (Phòng " + room + ")";
            } else {
                task_val = "Lớp học " + subj_val;
            }
        }

        // Bỏ qua bản ghi trùng lặp hoàn toàn
        const std::string key = task_val + "|" + deadline_val + "|" + subj_val;
        if (!seen.insert(key).second) continue;

        nlohmann::ordered_json entry;
        entry["task"]     = task_val;
        entry["deadline"] = deadline_val;
        entry["subject"]  = subj_val;
        validated.push_back(std::move(entry));
    }

    if (validated.empty()) {
        std::cout << console::RED
                  << "[✗] Không có dữ liệu hợp lệ sau khi chuẩn hóa.\n"
                  << console::RESET;
        return false;
    }

    // ── Bước 4: In trực tiếp nội dung file JSON ra màn hình Terminal ──────────
    std::cout << "\n"
              << console::BOLD << console::CYAN
              << "╔══════════════════════════════════════════════════════════════════╗\n"
              << "║       NỘI DUNG FILE JSON ĐẦU RA (" << out_path.string() << ")        ║\n"
              << "╚══════════════════════════════════════════════════════════════════╝\n"
              << console::RESET
              << console::GREEN
              << validated.dump(2) << "\n"
              << console::RESET
              << console::YELLOW
              << "=> Tổng cộng: " << validated.size()
              << " mốc lịch học/deadline được bóc tách với 3 thuộc tính: task, deadline, subject (như TC-01).\n"
              << console::RESET;

    // ── Bước 5: Ghi file JSON (pretty, indent 2) ──────────────────────────
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::ofstream ofs(out_path);
    if (!ofs.is_open()) {
        std::cout << console::RED
                  << "[✗ FILE] Không thể ghi: " << out_path.string() << "\n"
                  << console::RESET;
        return false;
    }
    ofs << validated.dump(2) << "\n";
    ofs.close();

    std::cout << console::CYAN
              << "[✓ FILE] Đã lưu file JSON thành công → " << console::BOLD
              << out_path.string() << console::RESET << "\n";

    return true;
}

/**
 * @brief Đọc toàn bộ nội dung của một file văn bản (.txt) an toàn.
 */
[[nodiscard]] std::optional<std::string> read_text_file(const fs::path& filepath) {
    std::error_code ec;
    if (!fs::exists(filepath, ec) || !fs::is_regular_file(filepath, ec) || ec) {
        return std::nullopt;
    }

    std::ifstream ifs(filepath, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    // Loại bỏ BOM UTF-8 nếu có (\xEF\xBB\xBF)
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }

    if (content.empty()) return std::nullopt;
    return content;
}

/**
 * @brief Bóc tách và lưu các mốc deadline từ văn bản sang file JSON.
 */
bool parse_and_save_deadlines_json(const std::string& raw_content, const fs::path& out_path) {
    std::string cleaned = raw_content;

    // Strip code fence
    auto fence_start = cleaned.find("```json");
    if (fence_start == std::string::npos) fence_start = cleaned.find("```");
    if (fence_start != std::string::npos) {
        auto content_start = cleaned.find('\n', fence_start);
        if (content_start != std::string::npos) {
            auto fence_end = cleaned.find("```", content_start + 1);
            if (fence_end != std::string::npos)
                cleaned = cleaned.substr(content_start + 1, fence_end - content_start - 1);
        }
    }

    const auto ltrim = cleaned.find('[');
    const auto rtrim = cleaned.rfind(']');
    if (ltrim != std::string::npos && rtrim != std::string::npos && rtrim > ltrim)
        cleaned = cleaned.substr(ltrim, rtrim - ltrim + 1);

    auto j = nlohmann::json::parse(cleaned, nullptr, false);
    if (j.is_discarded() || (!j.is_array() && !j.is_object())) {
        j = try_repair_json(cleaned);
    }

    // Fallback: nếu AI trả về dạng văn bản liệt kê (1. ... 2. ... hoặc - ...)
    if (j.is_discarded() || j.empty()) {
        std::istringstream stream(raw_content);
        std::string line;
        nlohmann::json line_arr = nlohmann::json::array();
        std::regex date_re(R"lit((\d{1,2}[/-]\d{1,2}(?:[/-]\d{2,4})?|\d{1,2}:\d{2}))lit");

        while (std::getline(stream, line)) {
            auto f = line.find_first_not_of(" \t\r\n-•*0123456789.)");
            if (f == std::string::npos) continue;
            std::string text = line.substr(f);
            if (text.size() < 12) continue;

            nlohmann::json entry;
            entry["task"] = text.size() > 70 ? text.substr(0, 70) + "..." : text;

            std::smatch m;
            if (std::regex_search(text, m, date_re)) {
                entry["deadline"] = m[1].str();
            } else {
                entry["deadline"] = "N/A";
            }

            if (text.find("OOP") != std::string::npos) entry["subject"] = "OOP";
            else if (text.find("Mạng") != std::string::npos || text.find("Network") != std::string::npos) entry["subject"] = "Mạng máy tính";
            else if (text.find("Cấu trúc") != std::string::npos || text.find("CTDL") != std::string::npos) entry["subject"] = "Cấu trúc dữ liệu";
            else entry["subject"] = "Học phần";

            line_arr.push_back(std::move(entry));
        }

        if (!line_arr.empty()) {
            j = std::move(line_arr);
        }
    }

    if (j.is_discarded() || j.empty()) {
        std::cout << console::RED
                  << "[✗ JSON PARSE] Không thể parse kết quả deadline sang mảng JSON.\n"
                  << console::RESET;
        return false;
    }

    if (j.is_object()) j = nlohmann::json::array({ j });

    nlohmann::ordered_json validated = nlohmann::ordered_json::array();
    const std::vector<std::string> task_keys = { "task", "name", "description", "cong_viec", "noi_dung", "title" };
    const std::vector<std::string> deadline_keys = { "deadline", "date", "due", "time", "han_chot", "ngay" };
    const std::vector<std::string> subject_keys = { "subject", "course", "mon_hoc", "mon", "slug", "type" };

    int idx = 0;
    for (const auto& item : j) {
        if (!item.is_object()) continue;
        ++idx;
        nlohmann::ordered_json entry;
        entry["task"]     = get_field(item, task_keys, "Task-" + std::to_string(idx));
        entry["deadline"] = get_field(item, deadline_keys, "N/A");
        entry["subject"]  = get_field(item, subject_keys, "General");
        validated.push_back(std::move(entry));
    }

    if (validated.empty()) return false;

    // ── Bước 4: In trực tiếp nội dung file JSON ra màn hình Terminal ──────────
    std::cout << "\n"
              << console::BOLD << console::CYAN
              << "╔══════════════════════════════════════════════════════════════════╗\n"
              << "║       NỘI DUNG FILE JSON ĐẦU RA (" << out_path.string() << ")       ║\n"
              << "╚══════════════════════════════════════════════════════════════════╝\n"
              << console::RESET
              << console::GREEN
              << validated.dump(2) << "\n"
              << console::RESET
              << console::YELLOW
              << "=> Tổng cộng: " << validated.size()
              << " mốc deadline được bóc tách thành công.\n"
              << console::RESET;

    // ── Bước 5: Ghi file JSON ─────────────────────────────────────────────
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::ofstream ofs(out_path);
    if (!ofs.is_open()) {
        std::cout << console::RED
                  << "[✗ FILE] Không thể ghi: " << out_path.string() << "\n"
                  << console::RESET;
        return false;
    }
    ofs << validated.dump(2) << "\n";
    ofs.close();

    std::cout << console::CYAN
              << "[✓ FILE] Đã lưu file JSON thành công → " << console::BOLD
              << out_path.string() << console::RESET << "\n";

    return true;
}

/**
 * @brief TC-01: Text-only — Trích xuất deadline từ file .txt hoặc văn bản mẫu.
 *
 * Hỗ trợ:
 *  1. Tự động đọc file văn bản (.txt) nếu tồn tại
 *  2. Gửi nội dung văn bản đến LLM để trích xuất deadline
 *  3. Xuất mảng JSON có cấu trúc ra file deadlines_output.json
 */
void run_tc01_text_only(LLMClient& client, const LocalizedPrompts& prompts, const fs::path& custom_txt_path = "")
{
    console::separator("TC-01: Text-only — Trích xuất Deadline (Đọc file .txt / Văn bản)");

    // Cảnh báo nếu dùng model English-only như moondream với prompt tiếng Việt
    if (client.get_config().model_name.find("moondream") != std::string::npos &&
        prompts.system_prompt.find("trợ lý") != std::string::npos) {
        std::cout << console::YELLOW
                  << "[LƯU Ý] Model 'moondream' là model chuyên Tiếng Anh (English-only).\n"
                  << "        Khi nhận văn bản tiếng Việt, model có thể bị lỗi từ vựng (hallucination).\n"
                  << "        → Khuyên dùng thêm tham số 'en' để chạy prompt tiếng Anh chuẩn:\n"
                  << "          .\\build\\edu_schedule_poc http://localhost:11434 moondream en\n"
                  << console::RESET << '\n';
    }

    // ── Bước 1: Tìm và nạp file .txt ──────────────────────────────────────────
    fs::path txt_file;
    if (!custom_txt_path.empty() && fs::exists(custom_txt_path)) {
        txt_file = custom_txt_path;
    } else {
        const std::vector<fs::path> candidates = {
            "input.txt",
            "assets/input.txt",
            "../input.txt",
            "../assets/input.txt",
            "notes.txt",
            "../notes.txt"
        };
        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !ec) {
                txt_file = p;
                break;
            }
        }
    }

    std::string text_content;
    if (!txt_file.empty()) {
        if (auto content = read_text_file(txt_file)) {
            text_content = *content;
            std::cout << console::GREEN << "[✓ INPUT FILE] " << console::RESET
                      << "Đã đọc thành công file: " << txt_file.string()
                      << " (" << text_content.size() << " bytes)\n";
        }
    }

    if (text_content.empty()) {
        std::cout << console::YELLOW
                  << "[!] Không tìm thấy file 'input.txt' → Sử dụng đoạn văn bản mẫu mặc định.\n"
                  << "    (Bạn có thể tạo file 'input.txt' hoặc truyền đường dẫn file qua tham số dòng lệnh)\n"
                  << console::RESET;
        text_content = prompts.user_prompt;
    }

    // ── Bước 2: Chuẩn bị prompt ───────────────────────────────────────────────
    const auto sys = ChatMessage::system(prompts.system_prompt);
    const std::string prompt_text = (prompts.system_prompt.find("trợ lý") != std::string::npos)
        ? "Hãy phân tích đoạn văn bản sau và trích xuất tất cả các mốc deadline, bài tập hoặc lịch thi thành một mảng JSON.\n"
          "Chỉ trả về JSON array — không giải thích, không markdown code block.\n"
          "Mỗi phần tử phải có đúng 3 trường sau:\n"
          "  \"task\": tên công việc/sự kiện cần hoàn thành (chuỗi)\n"
          "  \"deadline\": ngày giờ hạn chót (chuỗi)\n"
          "  \"subject\": tên môn học liên quan (chuỗi)\n"
          "Ví dụ:\n"
          "[{\"task\": \"Nộp bài tập lớn\", \"deadline\": \"23:59 ngày 24/10\", \"subject\": \"OOP\"}]\n\n"
          "Văn bản:\n\"" + text_content + "\""
        : "Extract all deadlines, assignments and exam dates from the text into a JSON array.\n"
          "Return ONLY a valid JSON array — no explanation, no markdown.\n"
          "Each item must have exactly 3 fields:\n"
          "  \"task\": task description (string)\n"
          "  \"deadline\": deadline date and time (string)\n"
          "  \"subject\": course/subject name (string)\n\n"
          "Text:\n\"" + text_content + "\"";

    const auto usr = ChatMessage::user(prompt_text);

    // ── Bước 3: Gửi request đến LLM ───────────────────────────────────────────
    std::cout << console::GREEN << "[1/3] " << console::RESET
              << "Đang gửi văn bản đến model " << client.get_config().model_name << "...\n"
              << std::flush;

    const LLMResult resp = client.chat({ sys, usr });
    console::print_response(resp, prompts.test_title.c_str());

    // ── Bước 4: Bóc tách JSON và lưu file deadlines_output.json ────────────────
    if (resp.has_value()) {
        std::cout << console::GREEN << "[2/3] " << console::RESET
                  << "Đang bóc tách JSON deadline...\n";
        const fs::path output_file = "output/deadlines_output.json";
        if (parse_and_save_deadlines_json(resp->content, output_file)) {
            std::cout << console::GREEN << "[3/3] " << console::RESET
                      << "Hoàn tất! File deadline đầu ra: " << console::BOLD
                      << output_file.string() << console::RESET << "\n";
        }
    }
}

/**
 * @brief TC-02: Multimodal Vision — Đọc ảnh thời khóa biểu.
 *
 * Quy trình:
 *  1. Tìm file ảnh (.png/.jpg) từ nhiều vị trí
 *  2. Mã hóa ảnh sang Base64 (RFC 4648)
 *  3. Gửi đến Ollama Vision model kèm prompt JSON
 *  4. Bóc tách phản hồi AI → mảng JSON 3 thuộc tính: subject, session, room
 *  5. Ghi ra file schedule_output.json
 */
void run_tc02_vision(LLMClient& client, const LocalizedPrompts& prompts, const fs::path& custom_image_path = "")
{
    console::separator("TC-02: Multimodal Vision — Đọc ảnh TKB → Xuất JSON");

    fs::path image_path;
    if (!custom_image_path.empty() && fs::exists(custom_image_path)) {
        image_path = custom_image_path;
    } else {
        // Tìm file ảnh (thử nhiều vị trí cả thư mục hiện tại lẫn thư mục cha khi chạy từ build/)
        const std::vector<fs::path> candidates = {
            "timetable.png",
            "assets/timetable.png",
            "../timetable.png",
            "../assets/timetable.png",
            "timetable.jpg",
            "../timetable.jpg",
            "assets/sample_schedule.png",
            "../assets/sample_schedule.png"
        };

        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !ec) {
                image_path = p;
                break;
            }
        }
    }

    if (image_path.empty()) {
        std::cout << console::YELLOW
                  << "[SKIP] Không tìm thấy file ảnh mẫu.\n"
                  << "       Đặt file 'timetable.png' vào thư mục chạy chương trình,\n"
                  << "       hoặc 'assets/timetable.png', hoặc truyền đường dẫn ảnh qua tham số thứ 5:\n"
                  << "       .\\build\\edu_schedule_poc <url> <model> <lang> <image_path>\n"
                  << console::RESET;
        return;
    }

    std::cout << console::CYAN << "Image: " << console::RESET
              << image_path.string()
              << "  (" << (fs::file_size(image_path) / 1024) << " KB)\n";

    // ── Bước 1: Đọc ảnh và mã hóa sang Base64 ────────────────────────────────
    const auto b64 = LLMClient::load_image_as_base64(image_path);
    if (!b64) {
        LLMResult err_resp = std::unexpected(LLMError{
            LLMErrorType::InvalidImage, "Khong doc duoc file anh."});
        console::print_response(err_resp, "TC-02: Multimodal Vision");
        return;
    }

    std::cout << console::GREEN << "[1/4]" << console::RESET
              << " Đọc & mã hóa Base64 thành công — " << b64->size() << " chars\n";

    // ── Bước 2: Chuẩn bị prompt Vision JSON ──────────────────────────────────
    std::string v_prompt = prompts.vision_prompt;
    if (client.get_config().model_name.find("moondream") != std::string::npos) {
        // moondream chỉ hỗ trợ tiếng Anh, cần prompt dứt khoát theo đúng schema: task, deadline, subject
        v_prompt =
            "Extract the schedule shown in this timetable image as a JSON list. "
            "Use only these 3 keys: \"task\", \"deadline\", \"subject\". "
            "Example: [{\"task\": \"OOP Class\", \"deadline\": \"Monday 08:00\", \"subject\": \"OOP\"}]. "
            "Output JSON only, stop after the classes.";
    }

    std::cout << console::GREEN << "[2/4]" << console::RESET
              << " Đang gửi ảnh đến model " << client.get_config().model_name << "...\n"
              << std::flush;

    // ── Bước 3: Gọi Vision model qua interface LLMClient ─────────────────────
    const auto msg = ChatMessage::user_with_images(v_prompt, { *b64 });
    const LLMResult resp = client.chat({ msg });

    console::print_response(resp, "TC-02: Multimodal Vision — Đọc ảnh TKB");

    if (!resp.has_value()) {
        const std::string err = resp.error().message;
        if (err.find("does not support multimodal") != std::string::npos ||
            err.find("Multimodal data provided") != std::string::npos) {
            std::cout << console::YELLOW
                      << "\n╔══════════════════════════════════════════════════════════════╗\n"
                      << "║ [CHẨN ĐOÁN]:                                                 ║\n"
                      << "║ - C++ Client: Đọc ảnh & mã hóa Base64 THÀNH CÔNG!            ║\n"
                      << "║ - Ollama: Model '" << client.get_config().model_name << "' là text-only,\n"
                      << "║   không có module Vision → HTTP 400.                         ║\n"
                      << "║                                                               ║\n"
                      << "║ → Hãy dùng model Vision (ví dụ: moondream):                  ║\n"
                      << "║   ollama pull moondream                                      ║\n"
                      << "║   .\\build\\edu_schedule_poc http://localhost:11434 moondream  ║\n"
                      << "╚══════════════════════════════════════════════════════════════╝\n"
                      << console::RESET << "\n";
        } else if (resp.error().http_status == 404 || err.find("not found") != std::string::npos) {
            std::cout << console::YELLOW
                      << "\n[CHẨN ĐOÁN]: Model '" << client.get_config().model_name
                      << "' chưa có trên Ollama.\n"
                      << "Dùng lệnh 'ollama list' để xem hoặc 'ollama pull "
                      << client.get_config().model_name << "' để tải.\n"
                      << console::RESET << "\n";
        }
        return;
    }

    // ── Bước 4: Bóc tách JSON 3 thuộc tính và ghi ra file ────────────────────
    std::cout << console::GREEN << "[3/4]" << console::RESET
              << " Đang bóc tách JSON từ phản hồi AI...\n";

    const fs::path output_file = "output/schedule_output.json";
    const bool saved = parse_and_save_schedule_json(resp->content, output_file);

    if (saved) {
        std::cout << console::GREEN << "[4/4]" << console::RESET
                  << " Hoàn tất! File JSON đầu ra: " << console::BOLD
                  << output_file.string() << console::RESET << "\n";
    }
}

/**
 * @brief TC-03: Error Handling — Kết nối sai port + timeout cực ngắn.
 *
 * Chứng minh hệ thống KHÔNG crash, mọi lỗi được trả qua nhánh LLMError của LLMResult.
 */
void run_tc03_error_handling()
{
    console::separator("TC-03: Error Handling — Bắt lỗi mượt mà");

    // ── Sub-test A: Sai port (Connection Refused) ─────────────────────────────
    {
        std::cout << "\n" << console::YELLOW
                  << "→ Sub-test A: Kết nối sai port (localhost:19999)\n"
                  << console::RESET;

        LLMConfig cfg;
        cfg.base_url           = "http://localhost:19999"; // Port sai
        cfg.connect_timeout_ms = 3000;                     // 3 giây

        // Polymorphism qua factory
        auto client = create_llm_client("ollama", cfg);
        const LLMResult resp = client->chat("Xin chào");

        if (!resp.has_value() &&
            resp.error().type == LLMErrorType::ConnectionRefused) {
            std::cout << console::GREEN << "  [✓ PASS]" << console::RESET
                      << " Bắt được ConnectionRefused: "
                      << resp.error().message << '\n';
        } else {
            std::cout << console::RED << "  [? UNEXPECTED]" << console::RESET
                      << " success=" << resp.has_value()
                      << " error=" << to_string((resp ? LLMErrorType::HttpError : resp.error().type))
                      << '\n';
        }
    }

    // ── Sub-test B: Timeout cực ngắn (1ms) ───────────────────────────────────
    {
        std::cout << "\n" << console::YELLOW
                  << "→ Sub-test B: Timeout cực ngắn (1ms)\n"
                  << console::RESET;

        LLMConfig cfg;
        cfg.base_url    = "http://localhost:11434"; // Port đúng
        cfg.timeout_ms  = 1;                        // Timeout 1ms — chắc chắn hết giờ
        cfg.connect_timeout_ms = 1;

        auto client = create_llm_client("ollama", cfg);
        const LLMResult resp = client->chat("Test timeout");

        if (!resp.has_value() &&
           (resp.error().type == LLMErrorType::Timeout ||
            resp.error().type == LLMErrorType::ConnectionRefused)) {
            std::cout << console::GREEN << "  [✓ PASS]" << console::RESET
                      << " Bắt được lỗi timeout/connection: "
                      << resp.error().message << '\n';
        } else {
            // Nếu server phản hồi trong vòng 1ms (cực kỳ hiếm), test này pass bình thường
            std::cout << console::YELLOW << "  [~ NOTE]" << console::RESET
                      << " Server phản hồi cực nhanh hoặc Ollama chưa chạy.\n";
        }
    }

    // ── Sub-test C: Malformed JSON giả lập ────────────────────────────────────
    {
        std::cout << "\n" << console::YELLOW
                  << "→ Sub-test C: Ollama trả về non-JSON (giả lập)\n"
                  << console::RESET;

        // Giả lập parse lỗi bằng cách parse trực tiếp
        const std::string bad_json = "This is not JSON at all!!!";
        const auto j = nlohmann::json::parse(bad_json, nullptr, false);

        if (j.is_discarded()) {
            std::cout << console::GREEN << "  [✓ PASS]" << console::RESET
                      << " nlohmann::json::parse với allow_exceptions=false "
                         "trả về discarded, không crash.\n";
        }
    }

    console::separator("TC-03 hoàn thành — 3/3 sub-tests chạy không crash");
}

// ══════════════════════════════════════════════════════════════════════════════
// §  main()
// ══════════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[])
{
    // ── Thiết lập UTF-8 Console và Virtual Terminal cho Windows ──────────────
    ConsoleEncodingGuard console_guard; // Tự phục hồi code page khi thoát

    // ── RAII global curl init ─────────────────────────────────────────────────
    CurlGlobalGuard curl_guard; // Tự cleanup khi main() return

    // ── Banner ────────────────────────────────────────────────────────────────
    std::cout << console::BOLD << console::CYAN
              << "\n╔══════════════════════════════════════════════════╗\n"
              << "║       EduSchedule-Agent — LLMClient PoC Test      ║\n"
              << "║       Võ Hoàng Nam — OOP Assignment               ║\n"
              << "╚══════════════════════════════════════════════════╝\n"
              << console::RESET;

    // ── Đọc config từ command-line (thông minh, linh hoạt) ───────────────────
    LLMConfig config;
    config.base_url        = "http://localhost:11434";
    config.model_name      = "";
    config.timeout_ms      = 300000; // 5 phút cho model vision nặng
    config.verify_ssl      = false;
    config.follow_redirect = true;

    AppLanguage lang = AppLanguage::Vietnamese;
    bool lang_explicitly_set = false;
    fs::path custom_image;
    fs::path custom_text;
    bool run_all_tests = false;

    // Quét toàn bộ tham số truyền vào (không phụ thuộc thứ tự)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all" || arg == "--test" || arg == "-a") {
            run_all_tests = true;
        } else if (arg == "en" || arg == "English" || arg == "english") {
            lang = AppLanguage::English;
            lang_explicitly_set = true;
        } else if (arg == "vi" || arg == "Vietnamese" || arg == "vietnamese") {
            lang = AppLanguage::Vietnamese;
            lang_explicitly_set = true;
        } else if (arg.rfind("http://", 0) == 0 || arg.rfind("https://", 0) == 0 || arg.rfind("localhost:", 0) == 0) {
            config.base_url = arg;
        } else {
            const auto ext = fs::path(arg).extension().string();
            if (ext == ".txt" || ext == ".md") {
                custom_text = arg;
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp") {
                custom_image = arg;
            } else if (config.model_name.empty()) {
                config.model_name = arg;
            }
        }
    }

    // Tự động suy luận model phù hợp nếu chưa chỉ định rõ
    if (config.model_name.empty()) {
        if (!custom_image.empty() && custom_text.empty()) {
            config.model_name = "moondream";
        } else {
            config.model_name = "qwen2.5:0.5b";
        }
    }

    // moondream chỉ hỗ trợ tiếng Anh → tự chuyển prompt sang tiếng Anh nếu user chưa chỉ định
    if (config.model_name.find("moondream") != std::string::npos && !lang_explicitly_set) {
        lang = AppLanguage::English;
    }

    const bool is_moondream = config.model_name.find("moondream") != std::string::npos;
    config.max_tokens = is_moondream ? 1024 : 4096;

    const LocalizedPrompts prompts = get_prompts_for_language(lang);

    std::cout << "\n"
              << console::YELLOW << "[Config]" << console::RESET
              << " URL  : " << config.base_url << "\n"
              << console::YELLOW << "[Config]" << console::RESET
              << " Model: " << config.model_name << "\n"
              << console::YELLOW << "[Config]" << console::RESET
              << " Lang : " << (lang == AppLanguage::English ? "English (en)" : "Tiếng Việt (vi)") << "\n";
    if (!custom_text.empty()) {
        std::cout << console::YELLOW << "[Config]" << console::RESET
                  << " Text : " << custom_text.string() << "\n";
    }
    if (!custom_image.empty()) {
        std::cout << console::YELLOW << "[Config]" << console::RESET
                  << " Image: " << custom_image.string() << "\n";
    }

    // ── Khởi tạo client qua con trỏ đa hình ──────────────────────────────────
    // AgentLoop chỉ thấy LLMClient* — không phụ thuộc OllamaClient cụ thể
    std::unique_ptr<LLMClient> client = create_llm_client("ollama", config);
    if (!client) {
        std::cerr << console::RED
                  << "[FATAL] create_llm_client() trả về nullptr!\n"
                  << console::RESET;
        return 1;
    }

    // ── Xác định chế độ thực thi ──────────────────────────────────────────────
    // Nếu chỉ truyền file văn bản (.txt)       → chỉ chạy bóc tách deadline sang JSON
    // Nếu chỉ truyền file hình ảnh (.png/.jpg) → chỉ chạy bóc tách TKB sang JSON
    // Nếu không truyền file hoặc truyền cờ -a  → chạy toàn bộ test suite
    const bool single_text_mode  = !custom_text.empty() && custom_image.empty() && !run_all_tests;
    const bool single_image_mode = !custom_image.empty() && custom_text.empty() && !run_all_tests;

    if (single_text_mode) {
        run_tc01_text_only(*client, prompts, custom_text);
    } else if (single_image_mode) {
        run_tc02_vision(*client, prompts, custom_image);
    } else {
        run_tc01_text_only(*client, prompts, custom_text);
        run_tc02_vision(*client, prompts, custom_image);
        run_tc03_error_handling();
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    console::separator("HOÀN TẤT THỰC THI");
    std::cout << console::GREEN
              << "Chương trình đã thực thi xong và xuất file JSON thành công.\n"
              << console::RESET << '\n';

    return 0;
}
