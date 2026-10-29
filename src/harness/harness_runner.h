#pragma once

#include "environment.h"
#include "evaluator.h"
#include "trajectory.h"
#include "agent/agent_loop.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace edu_schedule {

// ============================================================================
// BenchmarkResult: Kết quả đánh giá của một bài kiểm thử đơn lẻ
// ============================================================================
struct BenchmarkResult {
    std::string task_id;
    Trajectory trajectory;
    EvaluationResult evaluation;
    bool passed = false;
    long long duration_ms = 0;
};

// ============================================================================
// BatchSummary: Báo cáo tổng hợp sau khi chạy cả tập Benchmark (ví dụ 10 tasks)
// ============================================================================
struct BatchSummary {
    int total_tasks = 0;
    int passed_tasks = 0;
    double success_rate = 0.0;
    long long average_latency_ms = 0;
    std::vector<BenchmarkResult> results;
};

// ============================================================================
// HarnessRunner: Lớp điều phối toàn bộ chu trình đánh giá ("Giám thị cuộc thi")
// Đảm bảo: Agent hoàn toàn không biết đến Harness, Harness chỉ giám sát qua hook
// ============================================================================
class HarnessRunner {
public:
    HarnessRunner(
        std::shared_ptr<IEnvironment> environment,
        std::shared_ptr<IEvaluator> evaluator,
        std::filesystem::path outputDirectory = "output/trajectories"
    ) : environment_(std::move(environment)),
        evaluator_(std::move(evaluator)),
        outputDirectory_(std::move(outputDirectory)) {
        if (!environment_) {
            environment_ = std::make_shared<NativeEnvironment>();
        }
        if (!evaluator_) {
            evaluator_ = std::make_shared<KeywordEvaluator>();
        }
    }

    // ========================================================================
    // runTask: Chạy và đánh giá một tác vụ duy nhất
    // 1. Chuẩn bị môi trường (setup)
    // 2. Gắn Observer Hook thu thập Trajectory vào AgentLoop
    // 3. Cho Agent giải bài
    // 4. Giám khảo (Evaluator) chấm điểm
    // 5. Dọn dẹp môi trường (teardown) và xuất Trajectory JSON
    // ========================================================================
    BenchmarkResult runTask(AgentLoop& agent, const BenchmarkTask& task, std::string_view modelName = "default_model") {
        BenchmarkResult result;
        result.task_id = task.id;

        Trajectory trajectory(task.id, std::string(modelName));

        // Observer Pattern: Đăng ký lambda nhận sự kiện từng bước từ Agent
        agent.setStepHook([&trajectory](const StepEvent& event) {
            Step s;
            s.step_id = event.step_index;
            s.thought = event.thought;
            s.latency_ms = event.duration_ms;

            if (event.decision) {
                if (const auto* tool_call = std::get_if<ToolCallAction>(&(*event.decision))) {
                    s.action_type = "tool_call";
                    s.tool_name = tool_call->tool_name;
                    s.tool_args = tool_call->arguments;
                } else if (const auto* answer = std::get_if<FinalAnswerAction>(&(*event.decision))) {
                    s.action_type = "final_answer";
                    s.tool_name = "";
                    s.tool_args = answer->answer;
                }
            }

            if (event.tool_result) {
                s.tool_result = event.tool_result->message;
                s.tool_success = event.tool_result->success;
            }

            trajectory.addStep(std::move(s));
        });

        // 1. Thiết lập môi trường thi
        environment_->setup();

        const auto startTime = std::chrono::steady_clock::now();

        // 2. Agent thực thi tác vụ
        const std::string finalAnswer = agent.run(task.instruction);

        const auto endTime = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // 3. Hoàn tất dữ liệu Trajectory
        trajectory.setTotalTimeMs(duration);
        trajectory.setFinalAnswer(finalAnswer);
        const bool agentSucceeded = !finalAnswer.starts_with("Loi") &&
                                    !finalAnswer.starts_with("Dung do lap");
        trajectory.setSuccess(agentSucceeded);
        if (!agentSucceeded) {
            trajectory.setErrorMessage(finalAnswer);
        }

        // 4. Giám khảo (Evaluator) chấm điểm độc lập
        EvaluationResult evalResult = evaluator_->evaluate(trajectory, task);

        // 5. Dọn dẹp môi trường
        environment_->teardown();

        // 6. Lưu Trajectory ra file JSON
        const auto jsonPath = outputDirectory_ / ("trajectory_" + task.id + ".json");
        (void)trajectory.saveToFile(jsonPath);

        result.trajectory = std::move(trajectory);
        result.evaluation = std::move(evalResult);
        result.passed = result.evaluation.passed;
        result.duration_ms = duration;

        return result;
    }

    // ========================================================================
    // runBatch: Đánh giá hàng loạt tập bài thi và tính toán tỷ lệ thành công
    // ========================================================================
    BatchSummary runBatch(AgentLoop& agent, const std::vector<BenchmarkTask>& tasks, std::string_view modelName = "default_model") {
        BatchSummary summary;
        summary.total_tasks = static_cast<int>(tasks.size());

        long long totalLatency = 0;

        for (const auto& task : tasks) {
            BenchmarkResult res = runTask(agent, task, modelName);
            if (res.passed) {
                summary.passed_tasks++;
            }
            totalLatency += res.duration_ms;
            summary.results.push_back(std::move(res));
        }

        if (summary.total_tasks > 0) {
            summary.success_rate = static_cast<double>(summary.passed_tasks) / static_cast<double>(summary.total_tasks) * 100.0;
            summary.average_latency_ms = totalLatency / summary.total_tasks;
        }

        return summary;
    }

private:
    std::shared_ptr<IEnvironment> environment_;
    std::shared_ptr<IEvaluator> evaluator_;
    std::filesystem::path outputDirectory_;
};

} // namespace edu_schedule
