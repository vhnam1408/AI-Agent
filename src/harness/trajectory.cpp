#include "harness/trajectory.h"

#include <cstdio>
#include <format>
#include <fstream>
#include <memory>
#include <print>

namespace edu_schedule {

namespace {

struct FileDeleter {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using UniqueFilePtr = std::unique_ptr<std::FILE, FileDeleter>;

} // namespace

// ============================================================================
// Hiện thực Step & Trajectory (TASK-16)
// ============================================================================

nlohmann::json Step::toJson() const {
    return {
        {"step_id", step_id},
        {"thought", thought},
        {"action", {
            {"type", action_type},
            {"tool", tool_name},
            {"args", tool_args}
        }},
        {"tool_result", tool_result},
        {"tool_success", tool_success},
        {"tokens_used", tokens_used},
        {"latency_ms", latency_ms}
    };
}

Trajectory::Trajectory(std::string taskId, std::string modelName)
    : taskId_(std::move(taskId)), model_(std::move(modelName)) {}

void Trajectory::addStep(Step step) {
    totalTokens_ += step.tokens_used;
    steps_.push_back(std::move(step));
}

nlohmann::json Trajectory::toJson() const {
    nlohmann::json j;
    j["task_id"] = taskId_;
    j["model"] = model_;
    j["success"] = success_;
    j["total_tokens"] = totalTokens_;
    j["total_time_ms"] = totalTimeMs_;
    j["final_answer"] = finalAnswer_;
    if (!errorMessage_.empty()) {
        j["error_message"] = errorMessage_;
    }

    nlohmann::json stepsArray = nlohmann::json::array();
    for (const auto& step : steps_) {
        stepsArray.push_back(step.toJson());
    }
    j["steps"] = stepsArray;
    return j;
}

bool Trajectory::saveToFile(const std::filesystem::path& filePath) const {
    try {
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }
        std::ofstream outFile(filePath);
        if (!outFile.is_open()) {
            return false;
        }
        outFile << toJson().dump(2) << '\n';
        return outFile.good();
    } catch (...) {
        return false;
    }
}

// ============================================================================
// Hiện thực IcsExporter chuẩn RFC 5545 bằng C++20 std::format & C++23 std::print
// ============================================================================

std::string IcsExporter::formatUtcTimestamp(TimePoint tp) {
    const auto secTp = std::chrono::floor<std::chrono::seconds>(tp);
    const auto dayTp = std::chrono::floor<std::chrono::days>(secTp);
    const std::chrono::year_month_day ymd{dayTp};
    const std::chrono::hh_mm_ss hms{secTp - dayTp};

    return std::format(
        "{:04d}{:02d}{:02d}T{:02d}{:02d}{:02d}Z",
        static_cast<int>(ymd.year()),
        static_cast<unsigned>(ymd.month()),
        static_cast<unsigned>(ymd.day()),
        static_cast<int>(hms.hours().count()),
        static_cast<int>(hms.minutes().count()),
        static_cast<int>(hms.seconds().count()));
}

std::string IcsExporter::escapeText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case ';':
                escaped += "\\;";
                break;
            case ',':
                escaped += "\\,";
                break;
            case '\r':
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
                escaped += "\\n";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string IcsExporter::toIcsString(std::span<const IcsEvent> events, TimePoint dtStamp) {
    const std::string stampStr = formatUtcTimestamp(dtStamp);

    std::string out = std::format(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//EduSchedule-Agent//Study Planner//VI\r\n"
        "CALSCALE:GREGORIAN\r\n");

    for (const auto& ev : events) {
        if (!ev.slot.isValid() || ev.uid.empty()) {
            continue;
        }
        out += std::format(
            "BEGIN:VEVENT\r\n"
            "UID:{}\r\n"
            "DTSTAMP:{}\r\n"
            "DTSTART:{}\r\n"
            "DTEND:{}\r\n"
            "SUMMARY:{}\r\n"
            "DESCRIPTION:{}\r\n"
            "END:VEVENT\r\n",
            escapeText(ev.uid),
            stampStr,
            formatUtcTimestamp(ev.slot.start),
            formatUtcTimestamp(ev.slot.end),
            escapeText(ev.summary),
            escapeText(ev.description));
    }

    out += "END:VCALENDAR\r\n";
    return out;
}

bool IcsExporter::saveToFile(
    const std::filesystem::path& filePath,
    std::span<const IcsEvent> events,
    TimePoint dtStamp) {
    try {
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        const std::string pathStr = filePath.string();
        UniqueFilePtr file(std::fopen(pathStr.c_str(), "wb"));
        if (!file) {
            return false;
        }

        const std::string icsContent = toIcsString(events, dtStamp);
        std::print(file.get(), "{}", icsContent);
        return std::fflush(file.get()) == 0;
    } catch (...) {
        return false;
    }
}

} // namespace edu_schedule

