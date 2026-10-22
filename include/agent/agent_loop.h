#pragma once

#include "client/llm_client.h"
#include "agent/loop_detector.h"
#include "agent/Tool.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace edu_schedule {

// Chỉ khai báo tên lớp. Agent không cần biết cách Registry lưu các Tool.
class ToolRegistry;

struct ToolCallAction {
    std::string tool_name;
    std::string arguments;
};

struct FinalAnswerAction {
    std::string answer;
};

// Mỗi quyết định là một trong hai loại: gọi Tool hoặc trả lời cuối cùng.
using AgentDecision = std::variant<ToolCallAction, FinalAnswerAction>;

// Gửi sau mỗi bước, kể cả bước gặp lỗi hoặc bị dừng do lặp.
// observation là đầu vào của bước; tool_result là kết quả sau khi gọi Tool.
struct StepEvent {
    int step_index = 0;
    std::string thought;
    std::string observation;
    std::optional<AgentDecision> decision; // Rỗng nếu LLM không trả được quyết định.
    long long duration_ms = 0;
    bool is_loop_warning = false;
    std::string loop_message;
    std::optional<ToolResult> tool_result; // Rỗng nếu chưa gọi Tool.
    std::string error_message;            // Lỗi khiến Agent phải dừng.
};

// Harness nhận sự kiện qua hàm này; Agent không include file Harness.
// Tham chiếu chỉ dùng trong callback. Muốn giữ sự kiện thì bên nhận phải copy.
using StepHook = std::function<void(const StepEvent&)>;

class AgentLoop {
public:
    AgentLoop(std::shared_ptr<LLMClient> llm_client,
              int max_steps = 10,
              LoopDetector loop_detector = LoopDetector{});
    virtual ~AgentLoop() = default;

    void setStepHook(StepHook hook);

    // Mượn Registry, không sở hữu nó. Registry phải sống lâu hơn lượt run().
    void setToolRegistry(ToolRegistry& registry);

    // Template Method: giữ thứ tự Observe -> Think -> Act -> báo sự kiện.
    // Lớp con thay đổi từng bước bên dưới, không ghi đè run().
    std::string run(std::string_view user_goal);

protected:
    virtual void onStart(std::string_view user_goal);
    virtual std::string observe(const std::string& input);
    virtual std::optional<AgentDecision> think(int step, const std::string& observation);
    virtual ToolResult act(const AgentDecision& decision);
    virtual bool isTerminal(const AgentDecision& decision, int current_step);
    virtual void onFinished(const std::string& final_answer);

    // Parser văn bản cũ để tương thích. Parser JSON đầy đủ thuộc TASK-13.
    virtual AgentDecision parseDecision(const std::string& raw_text);

    std::shared_ptr<LLMClient> llm_client_;
    int max_steps_ = 10;
    LoopDetector loop_detector_;
    std::vector<ChatMessage> history_;
    StepHook step_hook_;
    ToolRegistry* tool_registry_ = nullptr;

private:
    void reportStep(StepEvent& event, std::chrono::steady_clock::time_point started_at);
};

} // namespace edu_schedule
