#include "harness/trajectory.h"
#include "harness/environment.h"
#include "harness/evaluator.h"
#include "harness/harness_runner.h"
#include "agent/agent_loop.h"
#include "client/llm_client.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace edu_schedule;

// Mock Agent phục vụ kiểm thử độc lập cho Harness (không phụ thuộc mạng hay Ollama)
class MockTestAgent : public AgentLoop {
public:
    explicit MockTestAgent(std::string simulatedAnswer)
        : AgentLoop(std::shared_ptr<LLMClient>{}, 3),
          simulatedAnswer_(std::move(simulatedAnswer)) {}

protected:
    std::optional<AgentDecision> think(int, const std::string&) override {
        return FinalAnswerAction{simulatedAnswer_};
    }

private:
    std::string simulatedAnswer_;
};

int main() {
    std::cout << "=== BẮT ĐẦU KIỂM THỬ TASK-08: HARNESS CONTRACT ===\n";

    // 1. Kiểm thử Environment
    NativeEnvironment nativeEnv;
    nativeEnv.setup();
    assert(!nativeEnv.isSandbox());
    nativeEnv.teardown();
    std::cout << "[PASS] NativeEnvironment hoạt động chính xác.\n";

    SandboxEnvironment sandboxEnv("test_sandbox");
    sandboxEnv.setup();
    assert(sandboxEnv.isSandbox());
    assert(sandboxEnv.isPathAllowed("test_sandbox/data.txt"));
    sandboxEnv.teardown();
    std::cout << "[PASS] SandboxEnvironment hoạt động chính xác.\n";

    // 2. Kiểm thử Trajectory & Step serialization
    Trajectory traj("task_test_001", "mock_model");
    Step step1;
    step1.step_id = 1;
    step1.thought = "Cần tra cứu hạn nộp bài";
    step1.action_type = "tool_call";
    step1.tool_name = "task_lookup";
    step1.tool_args = "T1";
    step1.tool_result = "Found task: OOP Assignment";
    step1.tool_success = true;
    step1.latency_ms = 120;
    traj.addStep(step1);

    traj.setFinalAnswer("Hoàn tất: Deadline là ngày 30/10.");
    traj.setSuccess(true);
    traj.setTotalTimeMs(500);

    const auto jsonOutput = traj.toJson();
    assert(jsonOutput["task_id"] == "task_test_001");
    assert(jsonOutput["steps"].size() == 1);
    assert(jsonOutput["steps"][0]["tool_result"] == "Found task: OOP Assignment");
    std::cout << "[PASS] Trajectory và Step xuất JSON chuẩn xác.\n";

    // 3. Kiểm thử Evaluator (Strategy Pattern)
    BenchmarkTask task;
    task.id = "task_test_001";
    task.instruction = "Tìm deadline bài tập OOP";
    task.expected_keywords = {"Deadline", "30/10"};
    task.max_steps = 5;

    KeywordEvaluator keywordEval;
    EvaluationResult evalRes = keywordEval.evaluate(traj, task);
    assert(evalRes.passed);
    assert(evalRes.score == 1.0);
    std::cout << "[PASS] KeywordEvaluator chấm đúng kết quả.\n";

    // 4. Kiểm thử HarnessRunner phối hợp cùng AgentLoop qua Observer Hook
    auto env = std::make_shared<NativeEnvironment>();
    auto evaluator = std::make_shared<KeywordEvaluator>();
    HarnessRunner runner(env, evaluator, "output/test_trajectories");

    MockTestAgent agent("Đáp án có Deadline và ngày 30/10 đầy đủ.");
    BenchmarkResult benchResult = runner.runTask(agent, task, "mock-vlm");

    assert(benchResult.passed);
    assert(benchResult.trajectory.stepCount() > 0);
    assert(benchResult.trajectory.getTaskId() == "task_test_001");
    std::cout << "[PASS] HarnessRunner điều phối AgentLoop qua Observer Hook thành công!\n";

    std::cout << "=== TẤT CẢ TEST TASK-08 ĐÃ VƯỢT QUA [100% PASS] ===\n";
    return 0;
}

