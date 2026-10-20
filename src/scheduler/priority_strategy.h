#pragma once

#include "tools/Task_domain.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - PriorityStrategy chỉ xếp hạng các Task đã vượt qua hard constraints.
// - Điểm ưu tiên không được phép hợp thức hóa lịch trùng, quá hạn hoặc sai dependency.
// - Strategy không sở hữu Task và không được giữ reference sau khi evaluate() kết thúc.

// Kết quả chấm điểm để Scheduler giải thích vì sao một Task được ưu tiên.
// Giá trị score càng lớn thì Task càng nên được xếp trước.
struct PriorityScore {
    double score = 0.0;
    std::optional<std::chrono::minutes> timeUntilDeadline;
    std::optional<std::chrono::minutes> slack;
    std::vector<std::string> reasons;
};

// Strategy Pattern: Scheduler phụ thuộc abstraction này, không phụ thuộc
// trực tiếp vào một công thức ưu tiên cụ thể.
class PriorityStrategy {
public:
    virtual ~PriorityStrategy() = default;

    [[nodiscard]]
    virtual std::string_view name() const noexcept = 0;

    [[nodiscard]]
    virtual PriorityScore evaluate(
        const Task& task,
        TimePoint now
    ) const = 0;
};

// Trọng số cho heuristic Priority + Urgency + Slack.
struct PriorityWeights {
    double importance = 1.0;
    double urgency = 1.0;
    double slack = 1.0;

    [[nodiscard]]
    bool isValid() const noexcept {
        return importance >= 0.0 && urgency >= 0.0 && slack >= 0.0;
    }
};

// Chiến lược mặc định cho MVP. Lớp này chỉ xếp hạng các Task đã hợp lệ;
// nó không được phép bỏ qua bất kỳ hard constraint nào.
class UrgencySlackPriorityStrategy final : public PriorityStrategy {
public:
    // Khi triển khai: weights không hợp lệ -> std::invalid_argument.
    explicit UrgencySlackPriorityStrategy(
        PriorityWeights weights = {}
    );

    [[nodiscard]]
    std::string_view name() const noexcept override;

    [[nodiscard]]
    PriorityScore evaluate(
        const Task& task,
        TimePoint now
    ) const override;

private:
    PriorityWeights weights_;
};

} // namespace edu_schedule
