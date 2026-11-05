#include "agent/agent_cli.h"
#include "agent/agent_loop.h"
#include "agent/ToolRegistry.h"
#include "client/ollama_client.h"
#include "tools/calculator_tool.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace edu_schedule {
namespace {
using Json = nlohmann::json;

// Adapter chỉ đổi JSON arguments thành input của CalculatorTool TASK-11.
class JsonCalculator final : public ITool {
public:
    std::string_view name() const noexcept override { return "calculator"; }
    std::string_view description() const noexcept override {
        return "Calculate arithmetic. Arguments: {\"expression\":\"15 + 17\"}. "
               "Use this tool for arithmetic instead of computing by yourself.";
    }
    ToolResult execute(std::string_view input) override {
        const auto value = Json::parse(input);
        if (!value.is_object() || value.size() != 1 || !value.contains("expression") ||
            !value["expression"].is_string()) return {false, "Expected a string expression."};
        return calculator_.execute(value["expression"].get<std::string>());
    }
private:
    CalculatorTool calculator_;
};

long positive(const std::string& value) {
    std::size_t end = 0;
    const auto result = std::stol(value, &end);
    if (end != value.size() || result < 1 || result > 2147483647L) {
        throw std::invalid_argument("Can so nguyen duong: " + value);
    }
    return result;
}

Json event_json(const StepEvent& event) {
    Json data = {{"step_index", event.step_index}, {"thought", event.thought},
        {"observation", event.observation}, {"duration_ms", event.duration_ms},
        {"is_loop_warning", event.is_loop_warning}, {"loop_message", event.loop_message},
        {"error_message", event.error_message}, {"decision", nullptr},
        {"tool_result", nullptr}, {"terminal_status", nullptr}, {"model_response", nullptr}};
    if (event.decision) {
        if (const auto* call = std::get_if<ToolCallAction>(&*event.decision)) {
            data["decision"] = {{"tool_name", call->tool_name}, {"arguments", call->arguments}};
        } else data["decision"] = {{"final_answer", std::get<FinalAnswerAction>(*event.decision).answer}};
    }
    if (event.tool_result) data["tool_result"] = {
        {"success", event.tool_result->success}, {"message", event.tool_result->message}};
    if (event.terminal_status) data["terminal_status"] = to_string(*event.terminal_status);
    if (event.model_response) {
        const auto& response = *event.model_response;
        data["model_response"] = {{"content", response.content}, {"model_used", response.model_used},
            {"truncated", response.truncated}, {"total_tokens", response.total_tokens},
            {"latency_ms", response.latency_ms}};
    }
    return data;
}
} // namespace

int run_agent_application(int argc, char* argv[]) {
    try {
        LLMConfig config;
        config.model_name.clear();
        config.temperature = 0.0f;
        std::string goal, output = "output/agent_result.json";
        int max_steps = 8;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "ai_agent --agent --model MODEL --goal TEXT [--url URL] "
                             "[--max-steps 8] [--output output/agent_result.json]\n"
                             "Options: --temperature --max-tokens --timeout-ms --connect-timeout-ms\n";
                return 0;
            }
            if (i + 1 == argc) throw std::invalid_argument("Thieu gia tri cho " + option);
            const std::string value = argv[++i];
            if (option == "--model") config.model_name = value;
            else if (option == "--url") config.base_url = value;
            else if (option == "--goal") goal = value;
            else if (option == "--output") output = value;
            else if (option == "--max-steps") max_steps = static_cast<int>(positive(value));
            else if (option == "--max-tokens") config.max_tokens = static_cast<int>(positive(value));
            else if (option == "--timeout-ms") config.timeout_ms = positive(value);
            else if (option == "--connect-timeout-ms") config.connect_timeout_ms = positive(value);
            else if (option == "--temperature") {
                std::size_t end = 0;
                const auto temperature = std::stod(value, &end);
                if (end != value.size() || !std::isfinite(temperature) || temperature < 0 || temperature > 2) {
                    throw std::invalid_argument("temperature: tu 0 den 2.");
                }
                config.temperature = static_cast<float>(temperature);
            } else throw std::invalid_argument("Tham so khong hop le: " + option);
        }
        if (config.model_name.find_first_not_of(" \t\r\n") == std::string::npos ||
            goal.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw std::invalid_argument("Can --model va --goal.");
        }
        CurlGlobalGuard guard;
        auto client = std::make_shared<OllamaClient>(config);
        ToolRegistry registry;
        registry.registerTool(std::make_unique<JsonCalculator>());
        registry.allowTool("calculator");
        AgentLoop agent(client, max_steps);
        agent.setToolRegistry(registry);
        Json events = Json::array();
        agent.setStepHook([&](const StepEvent& event) { events.push_back(event_json(event)); });
        agent.run(goal);
        const auto& result = agent.lastResult();
        Json history = Json::array();
        for (const auto& message : agent.history()) {
            history.push_back({{"role", message.role}, {"content", message.content}});
        }
        Json data = {{"task", "TASK-025"}, {"source", "ollama"},
            {"model_requested", config.model_name}, {"status", to_string(result.status)},
            {"success", result.success()}, {"answer", result.answer}, {"steps", result.steps},
            {"tool_calls", result.tool_calls}, {"events", events}, {"history", history}};
        const std::filesystem::path path(output);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Khong tao duoc file: " + output);
        file << data.dump(2) << '\n';
        file.close();
        if (!file) throw std::runtime_error("Khong ghi duoc file: " + output);
        std::cout << data.dump(2) << '\n';
        return result.success() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Loi Agent: " << error.what() << '\n';
        return 1;
    }
}
} // namespace edu_schedule
