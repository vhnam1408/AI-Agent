#include "client/tool_parser.h"
#include "client/ollama_client.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace edu_schedule;
using json = nlohmann::json;

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("Modes: parse FILE | image-check PNG | image PNG URL MODEL");
        const std::string mode = argv[1];
        if (mode == "parse") {
            std::ifstream file(argv[2], std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open raw response");
            std::string body{std::istreambuf_iterator<char>(file), {}};
            auto result = parse_chat_response(body);
            if (!result) throw std::runtime_error(result.error().message);
            json calls = json::array();
            for (const auto& call : result->calls)
                calls.push_back({{"tool_name", call.tool_name}, {"arguments", json::parse(call.arguments)}});
            std::cout << json{{"content", result->content}, {"calls", calls}}.dump(2) << '\n';
            return 0;
        }
        const auto b64 = LLMClient::load_image_as_base64(argv[2]);
        if (!b64 || !b64->starts_with("iVBORw0KGgo")) throw std::runtime_error("Expected nonempty PNG Base64");
        if (mode == "image-check") {
            // Output used by the offline transport test to verify ALL source bytes.
            std::cout << *b64 << '\n';
            return 0;
        }
        if (mode != "image" || argc != 5) throw std::runtime_error("image requires PNG URL MODEL");
        CurlGlobalGuard guard;
        LLMConfig config;
        config.base_url = argv[3]; config.model_name = argv[4];
        config.temperature = 0; config.max_tokens = 4096; config.timeout_ms = 300000;
        OllamaClient client(config);
        auto response = client.chat({ChatMessage::user_with_images(
            "Extract every timetable class as a raw JSON array. Each object has exactly: "
            "day (integer: Monday=2 to Sunday=8), kind (LT or TH), subject (uppercase Vietnamese "
            "exactly as printed), room (exact identifier plus campus, e.g. F203 (CS2)), "
            "start and end (null if no times are printed). Preserve all classes. "
            "Do not infer times, dates, periods, or deadlines from grid positions. Return JSON only.", {*b64})});
        if (!response) throw std::runtime_error(response.error().message);
        if (response->truncated) throw std::runtime_error("Truncated vision response");
        // Feed the actual C++ client result through the C++ parser too.
        auto parsed = parse_chat_response(json{{"message", {{"content", response->content}}}}.dump());
        if (!parsed || !parsed->calls.empty()) throw std::runtime_error("Invalid vision content");
        std::cout << json{{"model_used", response->model_used}, {"content", parsed->content},
                           {"latency_ms", response->latency_ms}}.dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
