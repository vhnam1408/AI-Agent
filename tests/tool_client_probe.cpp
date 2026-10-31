#include "client/ollama_client.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

using namespace edu_schedule;
using Json = nlohmann::json;
namespace {
// Chuyển calls thành mảng JSON, giải mã arguments và giữ id/index nếu có.
Json calls_json(const ParsedMessage& message) {
    Json calls = Json::array();
    for (const auto& call : message.calls) {
        Json value{{"tool_name", call.tool_name}, {"arguments", Json::parse(call.arguments)}};
        if (!call.id.empty()) value["id"] = call.id;
        if (call.index) value["index"] = *call.index;
        calls.push_back(std::move(value));
    }
    return calls;
}
// Đọc nguyên văn file fixture hoặc prompt; ném exception nếu không mở được file.
std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read file");
    return {std::istreambuf_iterator<char>(input), {}};
}
}

// Chọn chế độ parse fixture, gọi tool, hỏi text hoặc gửi ảnh theo tham số CLI.
// Chế độ ảnh nhận prompt và ảnh chi tiết tùy chọn; in JSON kết quả, trả 1 khi lỗi.
int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("parse FILE | tools URL MODEL [CASE] | text URL MODEL | image PNG URL MODEL [PROMPTFILE [DETAILPNG]]");
        const std::string mode = argv[1];
        if (mode == "parse") {
            auto result = parse_chat_response(read_file(argv[2]));
            if (!result) throw std::runtime_error(result.error().message);
            std::cout << Json{{"content", result->content}, {"calls", calls_json(*result)}}.dump(2) << '\n';
            return 0;
        }
        const bool image = mode == "image";
        if ((!image && argc < 4) || (image && argc < 5)) throw std::runtime_error("Missing Client arguments");
        CurlGlobalGuard guard;
        LLMConfig config;
        config.base_url = argv[image ? 3 : 2];
        config.model_name = argv[image ? 4 : 3];
        config.temperature = 0;
        config.max_tokens = image ? 4096 : 2048;
        config.timeout_ms = 900000;
        if (mode == "tools") {
            const auto parameters = Json::parse(R"({"type":"object","properties":{"task_id":{"type":"string"}},"required":["task_id"],"additionalProperties":false})");
            std::vector<ToolDefinition> tools{{"task_lookup", "Look up a task by exact ID", parameters}};
            const std::string scenario = argc >= 5 ? argv[4] : "valid";
            if (scenario == "no-tools") tools.clear();
            if (scenario == "bad-tool-name") tools[0].name = "../exec";
            if (scenario == "duplicate-tools") tools.push_back(tools[0]);
            if (scenario == "bad-schema") tools[0].parameters = Json::array();
            if (scenario == "bad-url") config.base_url = "file:///tmp/test";
            if (scenario == "bad-temperature") config.temperature = std::numeric_limits<float>::quiet_NaN();
            OllamaClient client(config);
            auto result = client.chat_with_tools(
                {ChatMessage::user("Call task_lookup with task_id T1. Use the tool; do not answer from memory.")}, tools);
            if (!result) {
                std::cout << Json{{"ok", false}, {"error_type", to_string(result.error().type)},
                    {"error", result.error().message}, {"http_status", result.error().http_status}}.dump(2) << '\n';
                return 1;
            }
            std::cout << Json{{"ok", true}, {"content", result->parsed.content}, {"calls", calls_json(result->parsed)},
                {"raw_response", Json::parse(result->raw_response)}, {"raw_response_text", result->raw_response},
                {"model_used", result->response.model_used},
                {"http_status", result->response.http_status}, {"latency_ms", result->response.latency_ms}}.dump(2) << '\n';
            return 0;
        }
        if (mode != "image" && mode != "text") throw std::runtime_error("Unknown mode");
        OllamaClient client(config);
        ChatMessage message;
        if (image) {
            auto encoded = LLMClient::load_image_as_base64(argv[2]);
            if (!encoded) throw std::runtime_error("Cannot load image");
            const auto prompt = argc >= 6 ? read_file(argv[5]) :
                "Extract every timetable class as a raw JSON array with exactly day (Monday=2 to Sunday=8), "
                "kind (LT or TH), subject (uppercase Vietnamese exactly as printed), room (exact identifier "
                "and campus), start and end (null when no hours are printed). Preserve all classes. "
                "Transcribe room identifiers character by character: preserve hyphens, underscores and "
                "periods exactly; never replace punctuation with another separator. Do not infer hours, "
                "periods, dates or deadlines. Return JSON only.";
            std::vector<std::string> images{*encoded};
            if (argc >= 7) {
                auto detail = LLMClient::load_image_as_base64(argv[6]);
                if (!detail) throw std::runtime_error("Cannot load detail image");
                images.push_back(*detail);
            }
            message = ChatMessage::user_with_images(prompt, std::move(images));
        } else {
            message = ChatMessage::user("Reply with only the number: 15 + 17");
        }
        auto response = client.chat({message});
        if (!response) throw std::runtime_error(response.error().message);
        if (response->truncated) throw std::runtime_error("Truncated response");
        std::cout << Json{{"content", response->content}, {"model_used", response->model_used},
            {"http_status", response->http_status}, {"latency_ms", response->latency_ms}}.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
