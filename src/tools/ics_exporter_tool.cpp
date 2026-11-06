#include "ics_exporter_tool.h"

#include "../harness/trajectory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace edu_schedule {

namespace {

std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\n\r");
    return std::string(s.substr(first, last - first + 1));
}

bool parseInt(std::string_view slice, int& out) noexcept {
    const auto [ptr, ec] = std::from_chars(slice.data(), slice.data() + slice.size(), out);
    return ec == std::errc{} && ptr == slice.data() + slice.size();
}

// Hỗ trợ cả chuẩn ISO-8601 ("2026-11-09T01:00:00Z") và RFC 5545 ("20261109T010000Z")
std::optional<TimePoint> parseUtcTime(std::string_view raw) noexcept {
    std::string_view s = raw;
    if (!s.empty() && (s.back() == 'Z' || s.back() == 'z')) {
        s.remove_suffix(1);
    }

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (s.size() == 15 && s[8] == 'T') {
        if (!parseInt(s.substr(0, 4), y) || !parseInt(s.substr(4, 2), mo) ||
            !parseInt(s.substr(6, 2), d) || !parseInt(s.substr(9, 2), h) ||
            !parseInt(s.substr(11, 2), mi) || !parseInt(s.substr(13, 2), sec)) {
            return std::nullopt;
        }
    } else if (s.size() == 19 && s[4] == '-' && s[7] == '-' &&
               (s[10] == 'T' || s[10] == ' ') && s[13] == ':' && s[16] == ':') {
        if (!parseInt(s.substr(0, 4), y) || !parseInt(s.substr(5, 2), mo) ||
            !parseInt(s.substr(8, 2), d) || !parseInt(s.substr(11, 2), h) ||
            !parseInt(s.substr(14, 2), mi) || !parseInt(s.substr(17, 2), sec)) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    const std::chrono::year_month_day ymd{
        std::chrono::year{y},
        std::chrono::month{static_cast<unsigned>(mo)},
        std::chrono::day{static_cast<unsigned>(d)}
    };
    if (!ymd.ok() || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) {
        return std::nullopt;
    }

    return std::chrono::sys_days{ymd}
         + std::chrono::hours{h}
         + std::chrono::minutes{mi}
         + std::chrono::seconds{sec};
}

void appendUniqueLink(std::vector<std::string>& links, std::string_view rawUrl) {
    const std::string url = trim(rawUrl);
    if (url.empty()) return;
    if (std::find(links.begin(), links.end(), url) == links.end()) {
        links.push_back(url);
    }
}

// Bóc các dòng "URL: https://..." từ kết quả trả về của WebSearchTool
void extractLinksFromSearchOutput(std::string_view text, std::vector<std::string>& links) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t endLine = text.find('\n', pos);
        const std::string_view line = (endLine == std::string_view::npos)
            ? text.substr(pos)
            : text.substr(pos, endLine - pos);

        const std::string trimmedLine = trim(line);
        if (trimmedLine.starts_with("URL:")) {
            appendUniqueLink(links, std::string_view(trimmedLine).substr(4));
        }
        if (endLine == std::string_view::npos) break;
        pos = endLine + 1;
    }
}

} // namespace

ICSExporterTool::ICSExporterTool(std::filesystem::path defaultOutputPath,
                                 std::shared_ptr<ITool> webSearchTool)
    : defaultOutputPath_(std::move(defaultOutputPath)),
      webSearchTool_(std::move(webSearchTool)) {}

std::string_view ICSExporterTool::name() const noexcept {
    return "ics_exporter";
}

std::string_view ICSExporterTool::description() const noexcept {
    return "Export study schedule to an RFC 5545 .ics calendar file with study reference URLs. "
           "Input JSON: {\"output_path\":\"...\",\"events\":[{\"summary\":\"...\",\"start_utc\":\"...\",\"end_utc\":\"...\",\"links\":[...],\"search_query\":\"...\"}]}.";
}

ToolResult ICSExporterTool::execute(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return {false, "Error: Input JSON cannot be empty."};
    }

    const auto payload = nlohmann::json::parse(trimmed, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return {false, "Error: Invalid JSON payload for ics_exporter."};
    }

    if (!payload.contains("events") || !payload["events"].is_array() || payload["events"].empty()) {
        return {false, "Error: Field 'events' must be a non-empty array."};
    }

    std::filesystem::path outPath = defaultOutputPath_;
    const std::string customPath = trim(payload.value("output_path", ""));
    if (!customPath.empty()) {
        if (customPath.find("..") != std::string::npos) {
            return {false, "Error: Path traversal ('..') is not allowed in output_path."};
        }
        outPath = customPath;
    }

    std::vector<IcsEvent> events;
    events.reserve(payload["events"].size());

    std::size_t index = 0;
    for (const auto& item : payload["events"]) {
        ++index;
        if (!item.is_object()) {
            return {false, "Error: Each element in 'events' must be a JSON object."};
        }

        const std::string summary = trim(item.value("summary", ""));
        if (summary.empty()) {
            return {false, "Error: Event summary cannot be empty."};
        }

        const auto startOpt = parseUtcTime(trim(item.value("start_utc", "")));
        const auto endOpt = parseUtcTime(trim(item.value("end_utc", "")));
        if (!startOpt.has_value() || !endOpt.has_value()) {
            return {false, "Error: Invalid start_utc or end_utc timestamp format."};
        }

        const TimeSlot slot{*startOpt, *endOpt};
        if (!slot.isValid()) {
            return {false, "Error: Event TimeSlot is invalid (start_utc must be before end_utc)."};
        }

        std::vector<std::string> links;
        if (item.contains("url") && item["url"].is_string()) {
            appendUniqueLink(links, item["url"].get<std::string>());
        }
        if (item.contains("links") && item["links"].is_array()) {
            for (const auto& linkItem : item["links"]) {
                if (linkItem.is_string()) {
                    appendUniqueLink(links, linkItem.get<std::string>());
                }
            }
        }

        const std::string searchQuery = trim(item.value("search_query", ""));
        if (!searchQuery.empty() && webSearchTool_ != nullptr) {
            const ToolResult searchRes = webSearchTool_->execute(searchQuery);
            if (searchRes.success) {
                extractLinksFromSearchOutput(searchRes.message, links);
            }
        }

        std::string fullDesc = trim(item.value("description", ""));
        if (!links.empty()) {
            if (!fullDesc.empty()) fullDesc += "\n\n";
            fullDesc += "Tài liệu tham khảo:";
            for (const auto& link : links) {
                fullDesc += "\n- " + link;
            }
        }

        std::string uid = trim(item.value("uid", ""));
        if (uid.empty()) {
            uid = "session-" + std::to_string(index) + "@eduschedule.agent";
        }

        events.push_back(IcsEvent{
            std::move(uid),
            summary,
            std::move(fullDesc),
            slot
        });
    }

    if (!IcsExporter::saveToFile(outPath, events)) {
        return {false, "Error: Failed to write .ics calendar file to " + outPath.string()};
    }

    std::ostringstream msg;
    msg << "Exported " << events.size() << " event(s) to " << outPath.string();
    return {true, msg.str()};
}

} // namespace edu_schedule
