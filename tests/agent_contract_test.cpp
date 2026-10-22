// Demo + kiểm thử khung Agent bằng dữ liệu giả; không gửi yêu cầu ra mạng.
#include "../src/client/llm_client.h"
#include "../src/agent/agent_loop.h"
#include "../src/agent/loop_detector.h"
#include "agent/ToolRegistry.h"

#include <iostream>
#include <stdexcept>
#include <utility>

using namespace edu_schedule;

void check(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

// Đóng vai LLM: lần đầu gọi Tool, lần sau trả đáp án hoặc tiếp tục gọi Tool.
class MockLLMClient : public LLMClient {
public:
    bool keep_calling = false;
    bool fail = false;
    int calls = 0;

    LLMResponse chat(const std::vector<ChatMessage>& messages) override {
        ++calls;
        if (fail) return {};
        if (calls == 2 && !keep_calling) {
            check(messages.back().content.find("Thong tin T1") != std::string::npos,
                  "Tool result must reach the next LLM step");
        }
        LLMResponse response;
        response.success = true;
        response.content = (calls == 1 || keep_calling) ? "mock_lookup" : "Da xong";
        return response;
    }

    void set_config(const LLMConfig& config) override { config_ = config; }
    const LLMConfig& get_config() const noexcept override { return config_; }

private:
    LLMConfig config_;
};

class DummyTool : public ITool {
public:
    int calls = 0;
    bool fail = false;
    bool throw_error = false;

    std::string_view name() const noexcept override { return "task_lookup"; }
    std::string_view description() const noexcept override { return "Du lieu gia"; }
    ToolResult execute(std::string_view input) override {
        ++calls;
        check(input == "T1", "Agent must forward Tool arguments unchanged");
        if (throw_error) throw std::runtime_error("Dummy Tool exception");
        return {!fail, fail ? "Khong tim thay Task" : "Thong tin T1"};
    }
};

// Chỉ mock parser, không cài parser JSON của TASK-13 trong test này.
// Đây cũng là ví dụ lớp con tùy biến một bước của Template Method.
class DemoAgent : public AgentLoop {
public:
    using AgentLoop::AgentLoop;

protected:
    AgentDecision parseDecision(const std::string& text) override {
        if (text == "mock_lookup") return ToolCallAction{"task_lookup", "T1"};
        return FinalAnswerAction{text};
    }
};

int main() {
    try {
        ToolRegistry registry;
        auto tool = std::make_unique<DummyTool>();
        auto* dummy = tool.get(); // Registry sở hữu Tool, con trỏ này chỉ dùng để kiểm tra.
        registry.registerTool(std::move(tool));
        registry.allowTool("task_lookup");

        auto client = std::make_shared<MockLLMClient>();
        DemoAgent agent(client, 3);
        agent.setToolRegistry(registry);
        std::vector<StepEvent> events;
        agent.setStepHook([&](const StepEvent& event) { events.push_back(event); });

        check(agent.run("Tra cuu T1") == "Da xong", "Agent did not finish");
        check(events.size() == 2 && dummy->calls == 1, "Unexpected steps or Tool calls");
        check(events[0].tool_result && events[0].tool_result->success,
              "Hook must receive the completed Tool result");
        check(events[0].tool_result->message == "Thong tin T1", "Wrong Tool result");
        check(!events[1].tool_result, "Final answer must not execute a Tool");
        check(events[0].duration_ms >= 0, "Step duration must be non-negative");
        std::cout << "[PASS] Mock LLM -> Registry -> DummyTool -> Hook -> Final answer\n";

        // Bước cuối vẫn chạy Tool; hết bước không được báo hoàn thành giả.
        client->keep_calling = true;
        DemoAgent limited(client, 1);
        limited.setToolRegistry(registry);
        events.clear();
        limited.setStepHook([&](const StepEvent& event) { events.push_back(event); });
        const int before = dummy->calls;
        check(limited.run("T1").find("gioi han") != std::string::npos, "Missing limit result");
        check(dummy->calls == before + 1 && events.size() == 1, "Last step was skipped");
        std::cout << "[PASS] max_steps executes the last Tool call and stops honestly\n";

        // Tool bị chặn, bị lỗi và chưa gắn Registry đều không được báo chạy thành công.
        registry.denyTool("task_lookup");
        limited.run("T1");
        check(!events.back().tool_result->success && dummy->calls == before + 1,
              "Agent bypassed Registry policy");
        registry.allowTool("task_lookup");
        dummy->fail = true;
        limited.run("T1");
        check(!events.back().tool_result->success, "Tool failure was lost");
        dummy->fail = false;
        dummy->throw_error = true;
        limited.run("T1");
        check(!events.back().tool_result->success, "Tool exception was lost");
        dummy->throw_error = false;

        DemoAgent missing_registry(client, 1);
        missing_registry.setStepHook([&](const StepEvent& event) { events.push_back(event); });
        missing_registry.run("T1");
        check(!events.back().tool_result->success, "Missing Registry must fail");
        std::cout << "[PASS] Registry denial, Tool failures, exceptions and missing Registry\n";

        // Các bước dừng sớm cũng phải xuất hiện trong Hook.
        client->fail = true;
        limited.run("T1");
        check(!events.back().decision && !events.back().error_message.empty(),
              "LLM failure event is missing");
        client->fail = false;
        DemoAgent repeated(client, 6);
        repeated.setToolRegistry(registry);
        events.clear();
        repeated.setStepHook([&](const StepEvent& event) { events.push_back(event); });
        const int repeat_before = dummy->calls;
        repeated.run("T1");
        check(events.size() == 4 && !events.back().error_message.empty(),
              "Critical loop event is missing");
        check(dummy->calls == repeat_before + 3 && !events.back().tool_result,
              "Critical loop must stop before executing the Tool");
        repeated.run("T1");
        check(events.size() == 8, "Loop history must reset between runs");
        std::cout << "[PASS] LLM error, critical loop events and reset between runs\n";

        bool rejected = false;
        try { DemoAgent invalid(client, 0); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid max_steps must be rejected");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
