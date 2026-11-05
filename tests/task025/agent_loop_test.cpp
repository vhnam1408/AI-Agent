// TASK-025: dùng AgentLoop và parser thật; chỉ LLM và tool là dữ liệu giả.
#include "agent/agent_loop.h"
#include "agent/ToolRegistry.h"
#include "harness/harness_runner.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace edu_schedule;
using Json = nlohmann::json;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
const std::string call = R"({"tool_name":"lookup","arguments":{"id":"T1"}})";
const std::string final = R"({"final_answer":"Da xong"})";

class ScriptedClient final : public LLMClient {
public:
    std::vector<std::string> replies;
    std::vector<std::vector<ChatMessage>> requests;
    int fail_on = -1;
    bool truncated = false;
    LLMResult chat(const std::vector<ChatMessage>& messages) override {
        const auto index = static_cast<int>(requests.size());
        requests.push_back(messages);
        if (index == fail_on) return make_error(LLMErrorType::Timeout, "test timeout");
        LLMResponse response;
        response.content = replies.at(static_cast<std::size_t>(index));
        response.truncated = truncated;
        response.model_used = "mock-task025";
        return response;
    }
    void set_config(const LLMConfig& value) override { config_ = value; }
    const LLMConfig& get_config() const noexcept override { return config_; }
private:
    LLMConfig config_;
};

class DummyTool final : public ITool {
public:
    int calls = 0;
    bool fail = false;
    bool throws = false;
    std::string last_input;
    std::string_view name() const noexcept override { return "lookup"; }
    std::string_view description() const noexcept override { return "Lookup with arguments {\"id\":\"T1\"}"; }
    ToolResult execute(std::string_view input) override {
        ++calls;
        last_input = input;
        check(Json::parse(input).at("id") == "T1", "Arguments were lost");
        if (throws) throw std::runtime_error("tool exception");
        return {!fail, fail ? "lookup failed" : "Thong tin T1"};
    }
};

struct Fixture {
    ToolRegistry registry;
    std::shared_ptr<ScriptedClient> client = std::make_shared<ScriptedClient>();
    AgentLoop agent;
    DummyTool* tool;
    std::vector<StepEvent> events;
    Fixture(std::vector<std::string> replies, int steps = 4)
        : agent(client, steps) {
        client->replies = std::move(replies);
        auto owned = std::make_unique<DummyTool>();
        tool = owned.get();
        registry.registerTool(std::move(owned));
        registry.allowTool("lookup");
        agent.setToolRegistry(registry);
        agent.setStepHook([this](const StepEvent& event) { events.push_back(event); });
    }
};
} // namespace

int main(int argc, char* argv[]) {
    Json results = Json::array();
    int passed = 0, failed = 0;
    const auto test = [&](const char* name, auto action) {
        try {
            action();
            ++passed;
            results.push_back({{"name", name}, {"status", "PASS"}});
        } catch (const std::exception& error) {
            ++failed;
            results.push_back({{"name", name}, {"status", "FAIL"}, {"error", error.what()}});
        }
    };
    test("react_json_history_and_hook", [] {
        Fixture f({call, final});
        check(f.agent.lastResult().status == AgentStatus::NotStarted, "Initial status");
        check(f.agent.run("Tra cuu T1") == "Da xong", "Wrong final answer");
        check(f.agent.lastResult().success() && f.agent.lastResult().steps == 2, "Wrong result");
        check(f.tool->calls == 1 && f.events.size() == 2, "Wrong calls/events");
        check(f.events[0].tool_result->message == "Thong tin T1", "Hook before tool");
        check(!f.events[1].tool_result && f.events[1].terminal_status == AgentStatus::Completed, "Final event");
        const auto& next = f.client->requests.at(1);
        check(next.back().content == "Observation: Thong tin T1", "Observation not forwarded");
        check(next.at(next.size() - 2).role == "assistant" && next.at(next.size() - 2).content == call, "Assistant history lost");
        check(f.agent.history().size() == 6, "Duplicate or missing history messages");
        check(f.events[0].model_response->model_used == "mock-task025", "Missing metadata");
        check(f.events[0].duration_ms >= 0, "Invalid duration");
    });
    test("max_steps_runs_last_tool_and_keeps_observation", [] {
        Fixture f({call}, 1);
        f.agent.run("T1");
        check(f.tool->calls == 1 && f.client->requests.size() == 1, "Last tool skipped or extra LLM call");
        check(f.agent.lastResult().status == AgentStatus::MaxStepsReached && !f.agent.lastResult().success(), "False success");
        check(f.events.back().terminal_status == AgentStatus::MaxStepsReached, "Missing terminal event");
        check(f.agent.history().back().content == "Observation: Thong tin T1", "Last observation missing");
    });
    test("final_on_last_step_is_completed", [] {
        Fixture f({call, final}, 2);
        f.agent.run("T1");
        check(f.agent.lastResult().success(), "Final answer on budget boundary failed");
    });
    test("final_without_tool", [] {
        Fixture f({final}, 1);
        f.agent.run("hello");
        check(f.tool->calls == 0 && f.agent.lastResult().success(), "Final answer called tool");
    });
    test("tool_failure_is_observed_then_recovered", [] {
        Fixture f({call, final});
        f.tool->fail = true;
        f.agent.run("T1");
        check(!f.events[0].tool_result->success && f.agent.lastResult().success(), "Tool failure not recoverable");
        check(f.client->requests[1].back().content.find("Loi Tool: lookup failed") != std::string::npos, "Missing tool error observation");
    });
    test("tool_exception_is_observed", [] {
        Fixture f({call, final});
        f.tool->throws = true;
        f.agent.run("T1");
        check(!f.events[0].tool_result->success && f.events[0].tool_result->message == "tool exception", "Exception lost");
    });
    test("denied_tool_never_executes", [] {
        Fixture f({call, final});
        f.registry.denyTool("lookup");
        f.agent.run("T1");
        check(f.tool->calls == 0 && !f.events[0].tool_result->success, "Policy bypass");
        check(f.client->requests[0][0].content.find("Lookup with arguments") == std::string::npos, "Denied tool advertised");
    });
    test("missing_registry_is_tool_failure", [] {
        auto client = std::make_shared<ScriptedClient>();
        client->replies = {call};
        AgentLoop agent(client, 1);
        std::vector<StepEvent> events;
        agent.setStepHook([&](const StepEvent& event) { events.push_back(event); });
        agent.run("T1");
        check(!events[0].tool_result->success && !agent.lastResult().success(), "Missing registry accepted");
    });
    test("llm_error_keeps_detail", [] {
        Fixture f({final});
        f.client->fail_on = 0;
        f.agent.run("T1");
        check(f.agent.lastResult().status == AgentStatus::LLMError, "Wrong LLM status");
        check(!f.events[0].decision && f.events[0].error_message.find("test timeout") != std::string::npos, "Error missing");
    });
    test("missing_client", [] {
        AgentLoop agent(nullptr);
        agent.run("T1");
        check(agent.lastResult().status == AgentStatus::LLMError, "Missing client accepted");
    });
    test("loop_warning_and_critical_stop", [] {
        Fixture f({call, call, call, call}, 6);
        f.agent.run("T1");
        check(f.events[1].is_loop_warning, "Warning missing");
        check(f.client->requests[2].back().content.find("Canh bao") != std::string::npos, "Warning not forwarded");
        check(f.tool->calls == 3 && !f.events.back().tool_result, "Critical executed tool");
        check(f.agent.lastResult().status == AgentStatus::LoopDetected, "Wrong loop status");
    });
    test("reset_history_detector_and_result_between_runs", [] {
        Fixture f({call, call, call, call}, 6);
        f.agent.run("OLD_GOAL");
        f.client->replies = {call, final};
        f.client->requests.clear();
        f.agent.run("NEW_GOAL");
        check(f.agent.lastResult().success() && f.agent.lastResult().steps == 2, "State not reset");
        check(f.tool->calls == 4, "Detector not reset");
        for (const auto& message : f.agent.history()) check(message.content.find("OLD_GOAL") == std::string::npos, "Old goal leaked");
    });
    for (const auto& [name, text] : std::vector<std::pair<std::string, std::string>>{
        {"malformed_json", "{bad"},
        {"duplicate_keys", R"({"tool_name":"lookup","arguments":{"id":"T1","id":"T2"}})"},
        {"unknown_tool", R"({"tool_name":"unknown","arguments":{}})"},
        {"invalid_arguments", R"({"tool_name":"lookup","arguments":42})"},
        {"mixed_final_and_tool", R"({"tool_name":"lookup","arguments":{"id":"T1"},"final_answer":"done"})"},
        {"multiple_calls_are_atomic_rejection", "[" + call + "," + call + "]"},
        {"empty_answer", "   "},
        {"empty_final_field", R"({"final_answer":" "})"},
        {"legacy_action_rejected", "Action: lookup"}}) {
        test(name.c_str(), [&] {
            Fixture f({text});
            f.agent.run("T1");
            check(f.agent.lastResult().status == AgentStatus::InvalidDecision, "Invalid decision accepted");
            check(f.tool->calls == 0 && !f.events[0].decision, "Invalid decision executed");
        });
    }
    test("truncated_reply_does_not_execute", [] {
        Fixture f({call});
        f.client->truncated = true;
        f.agent.run("T1");
        check(f.agent.lastResult().status == AgentStatus::InvalidDecision && f.tool->calls == 0, "Truncated call executed");
    });
    test("markdown_and_string_arguments_from_task013", [] {
        Fixture f({"```json\n{\"tool_name\":\"lookup\",\"arguments\":\"{\\\"id\\\":\\\"T1\\\"}\"}\n```", "```json\n" + final + "\n```"});
        f.agent.run("T1");
        check(f.agent.lastResult().success() && Json::parse(f.tool->last_input)["id"] == "T1", "Parser integration failed");
    });
    test("plain_text_final_compatibility", [] {
        Fixture f({"Hello"});
        check(f.agent.run("Hi") == "Hello" && f.agent.lastResult().success(), "Plain text rejected");
    });
    test("invalid_step_budget", [] {
        bool rejected = false;
        try { AgentLoop invalid(nullptr, 0); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Zero max_steps accepted");
    });
    test("harness_uses_status_and_releases_hook", [] {
        Fixture f({call}, 1);
        BenchmarkTask task;
        task.id = "task025_budget";
        task.instruction = "T1";
        HarnessRunner runner(std::make_shared<NativeEnvironment>(),
                             std::make_shared<KeywordEvaluator>(), "task025-trajectories");
        const auto result = runner.runTask(f.agent, task, "mock-task025");
        check(!result.trajectory.isSuccess(), "Harness counted max_steps as success");
        check(result.trajectory.stepCount() == 1, "Harness missed a step");
        // Sau runTask không còn callback giữ reference tới trajectory đã hết vòng đời.
        f.client->requests.clear();
        f.agent.run("T1");
        check(f.agent.lastResult().status == AgentStatus::MaxStepsReached, "Agent reuse failed");
    });
    const Json report = {{"task", "TASK-025"}, {"mode", "mock"}, {"passed", passed}, {"failed", failed}, {"tests", results}};
    std::cout << report.dump(2) << '\n';
    if (argc > 1) {
        std::ofstream output(argv[1]);
        output << report.dump(2) << '\n';
        if (!output) return 2;
    }
    return failed ? 1 : 0;
}
