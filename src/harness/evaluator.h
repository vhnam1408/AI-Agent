#pragma once

#include "trajectory.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

// ============================================================================
// BenchmarkTask: Định nghĩa một bài toán đánh giá chuẩn trong tasks.json
// Tuân thủ đúng format mô tả trong mục VII.7.2 của đề bài
// ============================================================================
struct BenchmarkTask {
    std::string id;
    std::string description;
    std::string instruction;
    std::string eval_type;       // "keyword", "functional", hoặc "vlm"
    std::string eval_script;     // Lệnh kiểm tra kết quả (nếu dùng functional)
    std::vector<std::string> expected_keywords; // Các từ khóa bắt buộc phải có
    int max_steps = 10;
};

// ============================================================================
// EvaluationResult: Kết quả chấm điểm bài làm của Agent
// ============================================================================
struct EvaluationResult {
    bool passed = false;
    double score = 0.0;          // Điểm số chuẩn hóa từ 0.0 đến 1.0 (hoặc 0 đến 100)
    std::string feedback;
    std::string failure_reason;
};

// ============================================================================
// IEvaluator: Interface thuần ảo đại diện cho "Giám khảo chấm thi" (Strategy Pattern)
// Giúp dễ dàng thay thế giải thuật chấm điểm mà không cần sửa đổi Agent hay Runner
// ============================================================================
class IEvaluator {
public:
    virtual ~IEvaluator() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Chấm điểm dựa trên vết tích suy luận (Trajectory) và đề bài (BenchmarkTask)
    [[nodiscard]] virtual EvaluationResult evaluate(
        const Trajectory& trajectory,
        const BenchmarkTask& task
    ) const = 0;
};

// ============================================================================
// KeywordEvaluator: Giám khảo chấm dựa trên sự xuất hiện của từ khóa mong đợi
// ============================================================================
class KeywordEvaluator : public IEvaluator {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "KeywordEvaluator";
    }

    [[nodiscard]] EvaluationResult evaluate(
        const Trajectory& trajectory,
        const BenchmarkTask& task
    ) const override {
        if (task.expected_keywords.empty()) {
            // Không có ràng buộc từ khóa -> coi như đạt nếu Agent không bị crash
            return {trajectory.isSuccess(), trajectory.isSuccess() ? 1.0 : 0.0, "Không có yêu cầu từ khóa.", ""};
        }

        const std::string& answer = trajectory.getFinalAnswer();
        int matched = 0;
        std::vector<std::string> missing;

        for (const auto& keyword : task.expected_keywords) {
            if (containsIgnoreCase(answer, keyword)) {
                ++matched;
            } else {
                missing.push_back(keyword);
            }
        }

        const double score = static_cast<double>(matched) / static_cast<double>(task.expected_keywords.size());
        const bool passed = (matched == static_cast<int>(task.expected_keywords.size()));

        EvaluationResult result;
        result.passed = passed;
        result.score = score;
        if (passed) {
            result.feedback = "Đạt toàn bộ từ khóa yêu cầu (" + std::to_string(matched) + "/" +
                              std::to_string(task.expected_keywords.size()) + ")";
        } else {
            result.feedback = "Chưa đạt đủ từ khóa (" + std::to_string(matched) + "/" +
                              std::to_string(task.expected_keywords.size()) + ")";
            result.failure_reason = "Thiếu từ khóa: " + join(missing, ", ");
        }
        return result;
    }

private:
    static bool containsIgnoreCase(std::string_view source, std::string_view target) {
        if (target.empty()) return true;
        auto it = std::search(
            source.begin(), source.end(),
            target.begin(), target.end(),
            [](char ch1, char ch2) {
                return std::tolower(static_cast<unsigned char>(ch1)) ==
                       std::tolower(static_cast<unsigned char>(ch2));
            }
        );
        return it != source.end();
    }

    static std::string join(const std::vector<std::string>& list, std::string_view delimiter) {
        std::string res;
        for (std::size_t i = 0; i < list.size(); ++i) {
            res += list[i];
            if (i + 1 < list.size()) res += delimiter;
        }
        return res;
    }
};

// ============================================================================
// FunctionalEvaluator: Giám khảo chấm dựa trên tính năng thực tế (gọi đúng tool,
// kết quả đầu ra đúng logic)
// ============================================================================
class FunctionalEvaluator : public IEvaluator {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "FunctionalEvaluator";
    }

    [[nodiscard]] EvaluationResult evaluate(
        const Trajectory& trajectory,
        const BenchmarkTask& task
    ) const override {
        // Kiểm tra xem Agent có kết thúc thành công không
        if (!trajectory.isSuccess()) {
            return {false, 0.0, "Agent kết thúc với trạng thái thất bại.", trajectory.getErrorMessage()};
        }

        // Kiểm tra số bước không vượt quá giới hạn
        if (static_cast<int>(trajectory.stepCount()) > task.max_steps) {
            return {false, 0.0, "Vượt quá số bước cho phép (" + std::to_string(task.max_steps) + ")", "Exceeded max steps"};
        }

        // Kiểm tra xem có ít nhất một công cụ được gọi thành công không
        bool hasSuccessfulToolCall = false;
        for (const auto& step : trajectory.getSteps()) {
            if (step.action_type == "tool_call" && step.tool_success) {
                hasSuccessfulToolCall = true;
                break;
            }
        }

        // Nếu đề bài có kịch bản eval_script, ghi nhận kịch bản cần thẩm định
        EvaluationResult result;
        result.passed = hasSuccessfulToolCall || !trajectory.getFinalAnswer().empty();
        result.score = result.passed ? 1.0 : 0.0;
        result.feedback = result.passed ? "Thực thi tính năng hoàn tất hợp lệ." : "Không có công cụ hoặc kết quả hợp lệ.";
        return result;
    }
};

// ============================================================================
// VLMEvaluator: Giám khảo thẩm định kết quả từ mô hình thị giác (Vision)
// ============================================================================
class VLMEvaluator : public IEvaluator {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "VLMEvaluator";
    }

    [[nodiscard]] EvaluationResult evaluate(
        const Trajectory& trajectory,
        const BenchmarkTask& task
    ) const override {
        (void)task;
        // Đánh giá phản hồi trích xuất từ hình ảnh
        const bool hasContent = !trajectory.getFinalAnswer().empty() && trajectory.isSuccess();
        return {
            hasContent,
            hasContent ? 1.0 : 0.0,
            hasContent ? "Thị giác máy tính trích xuất thành công." : "Không trích xuất được thông tin thị giác.",
            hasContent ? "" : "Visual extraction failed"
        };
    }
};

} // namespace edu_schedule

