#include "web_search_tool.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace edu_schedule {

namespace {

struct SearchItem {
    std::string title;
    std::string url;
    std::string snippet;
};

struct CurlEasyDeleter {
    void operator()(CURL* handle) const noexcept {
        if (handle != nullptr) curl_easy_cleanup(handle);
    }
};

struct CurlStringDeleter {
    void operator()(char* str) const noexcept {
        if (str != nullptr) curl_free(str);
    }
};

std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\n\r");
    return std::string(s.substr(first, last - first + 1));
}

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::size_t writeCallback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    const std::size_t total = size * nmemb;
    if (userdata != nullptr && ptr != nullptr) {
        static_cast<std::string*>(userdata)->append(ptr, total);
    }
    return total;
}

std::string fetchOnlineJson(const std::string& endpointUrl, std::string_view query) {
    if (endpointUrl.empty()) return {};

    std::unique_ptr<CURL, CurlEasyDeleter> curl(curl_easy_init());
    if (!curl) return {};

    std::unique_ptr<char, CurlStringDeleter> escaped(
        curl_easy_escape(curl.get(), query.data(), static_cast<int>(query.size()))
    );
    if (!escaped) return {};

    const char sep = (endpointUrl.find('?') == std::string::npos) ? '?' : '&';
    const std::string fullUrl = endpointUrl + sep + "q=" + escaped.get()
                              + "&format=json&no_html=1&skip_disambig=1";

    std::string body;
    curl_easy_setopt(curl.get(), CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "EduSchedule-Agent/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 4000L);

    const CURLcode res = curl_easy_perform(curl.get());
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

    if (res != CURLE_OK || (status != 0 && (status < 200 || status >= 300))) {
        return {};
    }
    return body;
}

std::string readMockFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::vector<SearchItem> extractItems(const nlohmann::json& j, std::string_view query, std::size_t limit) {
    std::vector<SearchItem> all;

    // 1. Cấu trúc SearXNG / Mock JSON chuẩn: {"results": [{"title", "url", "snippet"|"content"}]}
    const nlohmann::json* arr = nullptr;
    if (j.is_array()) {
        arr = &j;
    } else if (j.is_object() && j.contains("results") && j["results"].is_array()) {
        arr = &j["results"];
    }

    if (arr != nullptr) {
        for (const auto& item : *arr) {
            if (!item.is_object()) continue;
            SearchItem s;
            s.title = trim(item.value("title", ""));
            s.url = trim(item.value("url", ""));
            s.snippet = trim(item.value("snippet", item.value("content", "")));
            if (!s.title.empty() || !s.snippet.empty()) {
                all.push_back(std::move(s));
            }
        }
    }

    // 2. Cấu trúc DuckDuckGo Instant Answer API: AbstractText + RelatedTopics
    if (j.is_object()) {
        const std::string abstractText = trim(j.value("AbstractText", ""));
        if (!abstractText.empty()) {
            all.push_back({
                trim(j.value("Heading", std::string(query))),
                trim(j.value("AbstractURL", "")),
                abstractText
            });
        }
        if (j.contains("RelatedTopics") && j["RelatedTopics"].is_array()) {
            for (const auto& topic : j["RelatedTopics"]) {
                if (!topic.is_object()) continue;
                const std::string text = trim(topic.value("Text", ""));
                const std::string url = trim(topic.value("FirstURL", ""));
                if (!text.empty()) {
                    all.push_back({text.substr(0, std::min<std::size_t>(60, text.size())), url, text});
                }
            }
        }
    }

    if (all.empty()) return {};

    // Lọc ưu tiên các kết quả có chứa từ khóa truy vấn
    const std::string qLower = toLower(query);
    std::vector<SearchItem> matched;
    for (const auto& item : all) {
        const std::string haystack = toLower(item.title + " " + item.snippet + " " + item.url);
        if (haystack.find(qLower) != std::string::npos) {
            matched.push_back(item);
            if (matched.size() >= limit) break;
        }
    }

    if (!matched.empty()) return matched;

    if (all.size() > limit) all.resize(limit);
    return all;
}

} // namespace

WebSearchTool::WebSearchTool(std::filesystem::path mockJsonPath, std::string endpointUrl)
    : mockJsonPath_(std::move(mockJsonPath)), endpointUrl_(std::move(endpointUrl)) {}

std::string_view WebSearchTool::name() const noexcept {
    return "web_search";
}

std::string_view WebSearchTool::description() const noexcept {
    return "Search study references via DuckDuckGo/SearXNG with offline mock JSON fallback. "
           "Input: plain query or JSON {\"query\": \"...\", \"limit\": 3}.";
}

ToolResult WebSearchTool::execute(std::string_view input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return {false, "Error: Search query cannot be empty."};
    }

    std::string query = trimmed;
    std::size_t limit = 3;

    if (trimmed.starts_with('{')) {
        const auto req = nlohmann::json::parse(trimmed, nullptr, false);
        if (req.is_discarded() || !req.is_object()) {
            return {false, "Error: Invalid JSON input for web_search."};
        }
        query = trim(req.value("query", ""));
        const int reqLimit = req.value("limit", 3);
        if (reqLimit > 0) {
            limit = static_cast<std::size_t>(std::min(reqLimit, 10));
        }
    }

    if (query.empty()) {
        return {false, "Error: Search field 'query' cannot be empty."};
    }

    std::vector<SearchItem> results;
    std::string sourceLabel = "online";

    // Bước 1: Thử truy vấn trực tuyến qua libcurl (DuckDuckGo / SearXNG)
    const std::string liveRaw = fetchOnlineJson(endpointUrl_, query);
    if (!liveRaw.empty()) {
        const auto liveJson = nlohmann::json::parse(liveRaw, nullptr, false);
        if (!liveJson.is_discarded()) {
            results = extractItems(liveJson, query, limit);
        }
    }

    // Bước 2: Dự phòng đọc từ tệp Mock JSON khi mất mạng hoặc API không trả kết quả
    if (results.empty()) {
        const std::string mockRaw = readMockFile(mockJsonPath_);
        if (!mockRaw.empty()) {
            const auto mockJson = nlohmann::json::parse(mockRaw, nullptr, false);
            if (!mockJson.is_discarded()) {
                results = extractItems(mockJson, query, limit);
                sourceLabel = "mock_fallback";
            }
        }
    }

    if (results.empty()) {
        return {false, "No web search results found for query: " + query};
    }

    std::ostringstream out;
    out << "[web_search source=" << sourceLabel << " count=" << results.size() << "]\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        out << (i + 1) << ". " << results[i].title << "\n";
        if (!results[i].url.empty()) {
            out << "   URL: " << results[i].url << "\n";
        }
        if (!results[i].snippet.empty()) {
            out << "   Snippet: " << results[i].snippet << "\n";
        }
    }
    return {true, out.str()};
}

} // namespace edu_schedule
