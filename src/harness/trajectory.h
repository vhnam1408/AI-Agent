#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace edu_schedule {

// ============================================================================
// Step: Đại diện cho một bước trong chuỗi suy luận ReAct của Agent
// (Ghi lại hộp đen: Agent quan sát gì -> nghĩ gì -> gọi tool gì -> kết quả ra sao)
// ============================================================================
struct Step {
    int step_id = 0;
    std::string thought;
    std::string action_type;  // "tool_call" hoặc "final_answer"
    std::string tool_name;    // Tên tool được gọi (nếu có)
    std::string tool_args;    // Tham số truyền vào tool
    std::string tool_result;  // Kết quả tool trả về
    bool tool_success = true;
    int tokens_used = 0;
    long long latency_ms = 0;

    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json step_json = {
            {"step_id", step_id},
            {"thought", thought},
            {"action", {
                {"type", action_type},
                {"tool", tool_name},
                {"args", tool_args}
            }},
            {"tool_result", tool_result},
            {"tool_success", tool_success},
            {"tokens_used", tokens_used},
            {"latency_ms", latency_ms}
        };
        return step_json;
    }
};

// ============================================================================
// Trajectory: Toàn bộ lịch sử hành trình giải quyết một tác vụ của Agent
// Tuân thủ chuẩn định dạng Trajectory JSON mô tả trong mục VII.7.1 của đề bài
// ============================================================================
class Trajectory {
public:
    Trajectory() = default;
    explicit Trajectory(std::string taskId, std::string modelName = "")
        : taskId_(std::move(taskId)), model_(std::move(modelName)) {}

    [[nodiscard]] const std::string& getTaskId() const noexcept { return taskId_; }

    void setSuccess(bool success) noexcept { success_ = success; }
    [[nodiscard]] bool isSuccess() const noexcept { return success_; }

    void setTotalTimeMs(long long ms) noexcept { totalTimeMs_ = ms; }

    void setFinalAnswer(std::string answer) { finalAnswer_ = std::move(answer); }
    [[nodiscard]] const std::string& getFinalAnswer() const noexcept { return finalAnswer_; }

    void setErrorMessage(std::string error) { errorMessage_ = std::move(error); }
    [[nodiscard]] const std::string& getErrorMessage() const noexcept { return errorMessage_; }

    void addStep(Step step) {
        steps_.push_back(std::move(step));
    }

    [[nodiscard]] const std::vector<Step>& getSteps() const noexcept {
        return steps_;
    }

    [[nodiscard]] std::size_t stepCount() const noexcept {
        return steps_.size();
    }

    // Chuyển toàn bộ Trajectory sang cấu trúc JSON chuẩn của đề tài
    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json j;
        j["task_id"] = taskId_;
        j["model"] = model_;
        j["success"] = success_;
        j["total_tokens"] = totalTokens_;
        j["total_time_ms"] = totalTimeMs_;
        j["final_answer"] = finalAnswer_;
        if (!errorMessage_.empty()) {
            j["error_message"] = errorMessage_;
        }

        nlohmann::json stepsArray = nlohmann::json::array();
        for (const auto& step : steps_) {
            stepsArray.push_back(step.toJson());
        }
        j["steps"] = stepsArray;
        return j;
    }

    // Lưu file JSON xuống ổ đĩa an toàn
    [[nodiscard]] bool saveToFile(const std::filesystem::path& filePath) const {
        try {
            if (filePath.has_parent_path()) {
                std::filesystem::create_directories(filePath.parent_path());
            }
            std::ofstream outFile(filePath);
            if (!outFile.is_open()) return false;
            outFile << toJson().dump(2) << '\n';
            return outFile.good();
        } catch (...) {
            return false;
        }
    }

private:
    std::string taskId_;
    std::string model_;
    bool success_ = false;
    int totalTokens_ = 0;
    long long totalTimeMs_ = 0;
    std::string finalAnswer_;
    std::string errorMessage_;
    std::vector<Step> steps_;
};

} // namespace edu_schedule
