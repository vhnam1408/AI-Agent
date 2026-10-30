#pragma once

#include "scheduler/scheduler.h"

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

    [[nodiscard]] nlohmann::json toJson() const;
};

// ============================================================================
// Trajectory: Toàn bộ lịch sử hành trình giải quyết một tác vụ của Agent
// Tuân thủ chuẩn định dạng Trajectory JSON mô tả trong mục VII.7.1 của đề bài
// ============================================================================
class Trajectory {
public:
    Trajectory() = default;
    explicit Trajectory(std::string taskId, std::string modelName = "");

    [[nodiscard]] const std::string& getTaskId() const noexcept { return taskId_; }

    void setSuccess(bool success) noexcept { success_ = success; }
    [[nodiscard]] bool isSuccess() const noexcept { return success_; }

    void setTotalTimeMs(long long ms) noexcept { totalTimeMs_ = ms; }

    void setFinalAnswer(std::string answer) { finalAnswer_ = std::move(answer); }
    [[nodiscard]] const std::string& getFinalAnswer() const noexcept { return finalAnswer_; }

    void setErrorMessage(std::string error) { errorMessage_ = std::move(error); }
    [[nodiscard]] const std::string& getErrorMessage() const noexcept { return errorMessage_; }

    void addStep(Step step);

    [[nodiscard]] const std::vector<Step>& getSteps() const noexcept { return steps_; }

    [[nodiscard]] std::size_t stepCount() const noexcept { return steps_.size(); }

    // Chuyển toàn bộ Trajectory sang cấu trúc JSON chuẩn của đề tài
    [[nodiscard]] nlohmann::json toJson() const;

    // Lưu file JSON xuống ổ đĩa an toàn
    [[nodiscard]] bool saveToFile(const std::filesystem::path& filePath) const;

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

// ============================================================================
// IcsEvent & IcsExporter (TASK-16): Xuất lịch học ra định dạng .ics chuẩn RFC 5545
// Sử dụng cấu trúc TimeSlot (từ scheduler.h), C++20 std::format và C++23 std::print
// ============================================================================
struct IcsEvent {
    std::string uid;
    std::string summary;
    std::string description;
    TimeSlot slot;
};

class IcsExporter {
public:
    // Định dạng TimePoint sang chuỗi UTC chuẩn RFC 5545 (YYYYMMDDTHHMMSSZ) bằng std::format
    [[nodiscard]] static std::string formatUtcTimestamp(TimePoint tp);

    // Escape các ký tự đặc biệt (\, ;, ,, xuống dòng) trong trường TEXT theo chuẩn RFC 5545
    [[nodiscard]] static std::string escapeText(std::string_view text);

    // Sinh nội dung chuỗi .ics hoàn chỉnh (dùng CRLF \r\n và C++20 std::format)
    [[nodiscard]] static std::string toIcsString(
        std::span<const IcsEvent> events,
        TimePoint dtStamp = std::chrono::system_clock::now());

    // Ghi nội dung .ics xuống file bằng C++23 std::print (giữ nguyên CRLF \r\n)
    [[nodiscard]] static bool saveToFile(
        const std::filesystem::path& filePath,
        std::span<const IcsEvent> events,
        TimePoint dtStamp = std::chrono::system_clock::now());
};

} // namespace edu_schedule
