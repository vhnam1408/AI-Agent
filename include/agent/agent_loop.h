#pragma once

#include "client/llm_client.h"
#include "agent/loop_detector.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace edu_schedule {

// AI quyết định gọi một công cụ (Tool)
struct ToolCallAction {
    std::string tool_name;
    std::string arguments; 
};

// AI quyết định hoàn thành và đưa ra câu trả lời cuối cùng
struct FinalAnswerAction {
    std::string answer; 
};

// Quyết định của AI: Chỉ có thể là Gọi Tool HOẶC Trả lời xong
using AgentDecision = std::variant<ToolCallAction, FinalAnswerAction>;

// Dữ liệu ghi lại diễn biến của từng bước chạy
struct StepEvent {
    int           step_index{0};
    std::string   thought;
    std::string   observation;
    AgentDecision decision;
    long long     duration_ms{0};
    bool          is_loop_warning{false};
    std::string   loop_message;
};

// Hàm callback để bên ngoài (Harness/Giao diện) có thể theo dõi AI chạy
using StepHook = std::function<void(const StepEvent&)>;

class AgentLoop {
public:
    AgentLoop(
        std::shared_ptr<LLMClient> llm_client,
        int max_steps = 10,
        LoopDetector loop_detector = LoopDetector{}
    ) : llm_client_(std::move(llm_client)),
        max_steps_(max_steps),
        loop_detector_(std::move(loop_detector)) {}

    virtual ~AgentLoop() = default;

    // Đăng ký hàm theo dõi tiến trình chạy của từng bước
    void setStepHook(StepHook hook) { step_hook_ = std::move(hook); }

    // Bắt đầu chạy toàn bộ vòng lặp để giải quyết mục tiêu của người dùng
    std::string run(std::string_view user_goal) {
        onStart(user_goal);
        std::string current_observation = "Muc tieu: " + std::string(user_goal);
        std::string final_answer;

        for (int step = 1; step <= max_steps_; ++step) {
            const auto step_start = std::chrono::steady_clock::now();

            // Bước 1: Quan sát thông tin hiện tại
            current_observation = observe(current_observation);

            // Bước 2: Cho AI suy nghĩ và đưa ra quyết định
            auto decision_opt = think(step, current_observation);
            if (!decision_opt) {
                final_answer = "Loi: AI khong the suy nghi tiep tai buoc " + std::to_string(step);
                onFinished(final_answer);
                return final_answer;
            }

            const AgentDecision& decision = *decision_opt;

            // Kiểm tra xem có bị rơi vào vòng lặp vô tận không
            bool is_loop_warning = false;
            std::string loop_msg;
            if (std::holds_alternative<ToolCallAction>(decision)) {
                const auto& tc = std::get<ToolCallAction>(decision);
                auto loop_check = loop_detector_.recordAndCheck(tc.tool_name + ":" + tc.arguments);

                if (loop_check.isCritical()) {
                    final_answer = "Dung khan cap: " + loop_check.message;
                    onFinished(final_answer);
                    return final_answer;
                }
                if (loop_check.isWarning()) {
                    is_loop_warning = true;
                    loop_msg = loop_check.message;
                }
            }

            const auto step_end = std::chrono::steady_clock::now();
            const auto duration_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(step_end - step_start).count();

            // Báo cáo diễn biến bước này cho người quan sát
            if (step_hook_) {
                step_hook_({
                    step,
                    (std::holds_alternative<FinalAnswerAction>(decision) ? "Dua ra dap an" : "Goi cong cu"),
                    current_observation,
                    decision,
                    duration_ms,
                    is_loop_warning,
                    loop_msg
                });
            }

            // Bước 3: Kiểm tra xem AI đã hoàn thành xong việc chưa
            if (isTerminal(decision, step)) {
                final_answer = std::holds_alternative<FinalAnswerAction>(decision)
                    ? std::get<FinalAnswerAction>(decision).answer
                    : "Hoan thanh cong viec tai buoc " + std::to_string(step);
                onFinished(final_answer);
                return final_answer;
            }

            // Bước 4: Thực thi hành động gọi Tool và thu thập kết quả
            current_observation = act(decision);
            if (is_loop_warning) {
                current_observation += "\n[Luu y] " + loop_msg + " - Hay thu cach khac!";
            }
        }

        final_answer = "Dung vi da vuot qua gioi han " + std::to_string(max_steps_) + " buoc.";
        onFinished(final_answer);
        return final_answer;
    }

protected:
    // Khởi tạo trạng thái ban đầu trước khi bắt đầu vòng lặp
    virtual void onStart(std::string_view user_goal) {
        history_.clear();
        loop_detector_.reset();
        history_.push_back(ChatMessage::system("You are an assistant. Think -> Act -> Observe."));
        history_.push_back(ChatMessage::user(std::string(user_goal)));
    }

    // Tiếp nhận và tiền xử lý thông tin quan sát
    virtual std::string observe(const std::string& input) { return input; }

    // Gửi lịch sử đến LLM để nhận suy nghĩ và quyết định của AI
    virtual std::optional<AgentDecision> think(int step, const std::string& current_observation) {
        (void)step;
        if (!llm_client_) return std::nullopt;
        history_.push_back(ChatMessage::user("Observation: " + current_observation));
        LLMResponse resp = llm_client_->chat(history_);
        if (!resp.success) return std::nullopt;
        history_.push_back(ChatMessage::assistant(resp.content));
        return parseDecision(resp.content);
    }

    // Thực hiện hành vi gọi Tool
    virtual std::string act(const AgentDecision& decision) {
        if (std::holds_alternative<ToolCallAction>(decision)) {
            const auto& tc = std::get<ToolCallAction>(decision);
            return "[Ket qua tu " + tc.tool_name + "]: Da chay voi tham so: " + tc.arguments;
        }
        return "";
    }

    // Kiểm tra xem vòng lặp đã đến lúc kết thúc chưa
    virtual bool isTerminal(const AgentDecision& decision, int current_step) {
        if (std::holds_alternative<FinalAnswerAction>(decision)) return true;
        return current_step >= max_steps_;
    }

    // Xử lý sau khi vòng lặp kết thúc hoàn toàn
    virtual void onFinished(const std::string& final_answer) {
        history_.push_back(ChatMessage::assistant("Final Answer: " + final_answer));
    }

    // Đọc văn bản của AI để nhận diện xem AI muốn gọi Tool hay đưa ra câu trả lời
    virtual AgentDecision parseDecision(const std::string& raw_text) {
        auto pos = raw_text.find("Action:");
        if (pos != std::string::npos) {
            std::string tool_name = raw_text.substr(pos + 7);
            auto line_end = tool_name.find('\n');
            if (line_end != std::string::npos) tool_name = tool_name.substr(0, line_end);
            return ToolCallAction{ tool_name, "{}" };
        }
        return FinalAnswerAction{ raw_text };
    }

protected:
    std::shared_ptr<LLMClient>   llm_client_;
    int                          max_steps_{10};
    LoopDetector                 loop_detector_;
    std::vector<ChatMessage>     history_;
    StepHook                     step_hook_{nullptr};
};

} // namespace edu_schedule
