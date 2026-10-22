#include "agent/agent_loop.h"
#include "agent/ToolRegistry.h"

#include <stdexcept>
#include <utility>

namespace edu_schedule {

AgentLoop::AgentLoop(std::shared_ptr<LLMClient> llm_client, int max_steps,
                     LoopDetector loop_detector)
    : llm_client_(std::move(llm_client)), max_steps_(max_steps),
      loop_detector_(std::move(loop_detector)) {
    if (max_steps < 1) throw std::invalid_argument("max_steps phai lon hon 0");
}

void AgentLoop::setStepHook(StepHook hook) {
    step_hook_ = std::move(hook);
}

void AgentLoop::setToolRegistry(ToolRegistry& registry) {
    tool_registry_ = &registry;
}

std::string AgentLoop::run(std::string_view user_goal) {
    onStart(user_goal);
    std::string observation = "Muc tieu: " + std::string(user_goal);
    std::string final_answer;

    for (int step = 1; step <= max_steps_; ++step) {
        const auto started_at = std::chrono::steady_clock::now();
        StepEvent event;
        event.step_index = step;

        // 1. Quan sát và yêu cầu LLM đưa ra quyết định.
        event.observation = observe(observation);
        event.decision = think(step, event.observation);
        if (!event.decision) {
            event.error_message = "Loi: LLM khong tra duoc quyet dinh.";
            reportStep(event, started_at);
            onFinished(event.error_message);
            return event.error_message;
        }

        const auto& decision = *event.decision;
        const auto* tool_call = std::get_if<ToolCallAction>(&decision);
        // Đây là nhãn hành động, không phải nội dung suy luận nội bộ của model.
        event.thought = tool_call ? "Goi cong cu" : "Dua ra dap an";

        // 2. Kiểm tra lặp trước khi cho phép thực thi Tool.
        if (tool_call) {
            const auto check = loop_detector_.recordAndCheck(
                tool_call->tool_name + ":" + tool_call->arguments);
            event.is_loop_warning = check.isWarning();
            event.loop_message = check.message;
            if (check.isCritical()) {
                event.error_message = "Dung do lap: " + check.message;
                reportStep(event, started_at);
                onFinished(event.error_message);
                return event.error_message;
            }
        }

        // 3. Có đáp án thì kết thúc; nếu chưa thì gọi Tool qua Registry.
        if (isTerminal(decision, step)) {
            const auto* answer = std::get_if<FinalAnswerAction>(&decision);
            final_answer = answer ? answer->answer : "Dung theo dieu kien cua Agent.";
            reportStep(event, started_at);
            onFinished(final_answer);
            return final_answer;
        }

        event.tool_result = act(decision);
        const auto& result = *event.tool_result;
        observation = result.success ? result.message : "Loi Tool: " + result.message;
        if (event.is_loop_warning) {
            observation += "\nCanh bao: " + event.loop_message + " Hay thu cach khac.";
        }

        // 4. Báo sau khi Tool chạy: sự kiện có kết quả và thời gian cả bước.
        reportStep(event, started_at);
    }

    // Đạt giới hạn không đồng nghĩa đã hoàn thành mục tiêu.
    final_answer = "Dung vi da dat gioi han " + std::to_string(max_steps_) + " buoc.";
    onFinished(final_answer);
    return final_answer;
}

void AgentLoop::reportStep(StepEvent& event,
                           std::chrono::steady_clock::time_point started_at) {
    event.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (step_hook_) step_hook_(event);
}

void AgentLoop::onStart(std::string_view user_goal) {
    history_.clear();
    loop_detector_.reset();
    history_.push_back(ChatMessage::system("You are an assistant. Think -> Act -> Observe."));
    history_.push_back(ChatMessage::user(std::string(user_goal)));
}

std::string AgentLoop::observe(const std::string& input) {
    return input;
}

std::optional<AgentDecision> AgentLoop::think(int, const std::string& observation) {
    if (!llm_client_) return std::nullopt;
    history_.push_back(ChatMessage::user("Observation: " + observation));
    const LLMResponse response = llm_client_->chat(history_);
    if (!response.success) return std::nullopt;
    history_.push_back(ChatMessage::assistant(response.content));
    return parseDecision(response.content);
}

ToolResult AgentLoop::act(const AgentDecision& decision) {
    const auto* call = std::get_if<ToolCallAction>(&decision);
    if (!call) return {false, "Quyet dinh khong phai lenh goi Tool."};
    if (!tool_registry_) return {false, "Chua gan ToolRegistry cho Agent."};
    try {
        return tool_registry_->execute(call->tool_name, call->arguments);
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}

bool AgentLoop::isTerminal(const AgentDecision& decision, int) {
    // Giới hạn bước do run() kiểm soát; không bỏ qua Tool ở bước cuối.
    return std::holds_alternative<FinalAnswerAction>(decision);
}

void AgentLoop::onFinished(const std::string& final_answer) {
    history_.push_back(ChatMessage::assistant("Final Answer: " + final_answer));
}

AgentDecision AgentLoop::parseDecision(const std::string& raw_text) {
    const auto position = raw_text.find("Action:");
    if (position == std::string::npos) return FinalAnswerAction{raw_text};

    std::string name = raw_text.substr(position + 7);
    name = name.substr(0, name.find('\n'));
    const auto first = name.find_first_not_of(" \t\r");
    if (first == std::string::npos) return ToolCallAction{"", "{}"};
    const auto last = name.find_last_not_of(" \t\r");
    // Giữ định dạng cũ: chưa đọc arguments JSON, không dùng như parser hoàn chỉnh.
    return ToolCallAction{name.substr(first, last - first + 1), "{}"};
}

} // namespace edu_schedule
