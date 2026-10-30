#include "harness/trajectory.h"
#include "harness/environment.h"
#include "harness/evaluator.h"
#include "harness/harness_runner.h"
#include "agent/agent_loop.h"
#include "client/llm_client.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

using namespace edu_schedule;

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

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
    check(!nativeEnv.isSandbox(), "NativeEnvironment khong phai sandbox");
    nativeEnv.teardown();
    std::cout << "[PASS] NativeEnvironment hoạt động chính xác.\n";

    SandboxEnvironment sandboxEnv("test_sandbox");
    sandboxEnv.setup();
    check(sandboxEnv.isSandbox(), "SandboxEnvironment phai la sandbox");
    check(sandboxEnv.isPathAllowed("test_sandbox/data.txt"), "Duong dan hop le trong sandbox bi chan");
    sandboxEnv.teardown();
    std::error_code ec;
    std::filesystem::remove_all("test_sandbox", ec);
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
    check(jsonOutput["task_id"] == "task_test_001", "Sai task_id");
    check(jsonOutput["steps"].size() == 1, "Sai so luong steps");
    check(jsonOutput["steps"][0]["tool_result"] == "Found task: OOP Assignment", "Sai tool_result");
    std::cout << "[PASS] Trajectory và Step xuất JSON chuẩn xác.\n";

    // 3. Kiểm thử Evaluator (Strategy Pattern)
    BenchmarkTask task;
    task.id = "task_test_001";
    task.instruction = "Tìm deadline bài tập OOP";
    task.expected_keywords = {"Deadline", "30/10"};
    task.max_steps = 5;

    KeywordEvaluator keywordEval;
    EvaluationResult evalRes = keywordEval.evaluate(traj, task);
    check(evalRes.passed, "KeywordEvaluator cham sai");
    check(evalRes.score == 1.0, "Diem KeywordEvaluator sai");
    std::cout << "[PASS] KeywordEvaluator chấm đúng kết quả.\n";

    // 4. Kiểm thử HarnessRunner phối hợp cùng AgentLoop qua Observer Hook
    auto env = std::make_shared<NativeEnvironment>();
    auto evaluator = std::make_shared<KeywordEvaluator>();
    HarnessRunner runner(env, evaluator, "output/test_trajectories");

    MockTestAgent agent("Đáp án có Deadline và ngày 30/10 đầy đủ.");
    BenchmarkResult benchResult = runner.runTask(agent, task, "mock-vlm");

    check(benchResult.passed, "HarnessRunner runTask that bai");
    check(benchResult.trajectory.stepCount() > 0, "HarnessRunner khong thu thap duoc step");
    check(benchResult.trajectory.getTaskId() == "task_test_001", "Sai task_id trong ket qua");
    std::filesystem::remove_all("output", ec);
    std::cout << "[PASS] HarnessRunner điều phối AgentLoop qua Observer Hook thành công!\n";

    std::cout << "=== TẤT CẢ TEST TASK-08 ĐÃ VƯỢT QUA [100% PASS] ===\n";
    return 0;
}
