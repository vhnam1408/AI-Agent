#include "scheduler/priority_strategy.h"

#include <algorithm>
#include <stdexcept>

namespace edu_schedule {
namespace {

// Chuyển Priority dạng enum thành một giá trị số để cộng vào score.
double importanceValue(Priority priority) noexcept {
    switch (priority) {
    case Priority::Low:
        return 1.0;
    case Priority::Mid:
        return 2.0;
    case Priority::High:
        return 3.0;
    }

    // Phòng trường hợp enum nhận một giá trị không hợp lệ.
    return 0.0;
}

// Deadline càng gần thì urgency càng lớn.
double urgencyValue(
    std::chrono::minutes timeUntilDeadline
) noexcept {
    const double minutes =
        static_cast<double>(timeUntilDeadline.count());

    // Deadline đã tới hoặc đã qua: cho điểm rất cao.
    if (minutes <= 0.0) {
        return 1'000.0 + std::min(-minutes, 1'000.0);
    }

    // 1'440 là số phút trong một ngày.
    // minutes càng nhỏ thì kết quả phép chia càng lớn.
    return 1'440.0 / (minutes + 60.0);
}

// Slack càng nhỏ thì Task càng ít thời gian dự phòng.
double slackValue(std::chrono::minutes slack) noexcept {
    const double minutes = static_cast<double>(slack.count());

    // Slack âm hoặc bằng 0 nghĩa là Task không còn thời gian dự phòng.
    if (minutes <= 0.0) {
        return 500.0 + std::min(-minutes, 500.0);
    }

    return 720.0 / (minutes + 60.0);
}

} // namespace

UrgencySlackPriorityStrategy::UrgencySlackPriorityStrategy(
    PriorityWeights weights
) : weights_(weights) {
    if (!weights_.isValid()) {
        throw std::invalid_argument(
            "Priority weights must be non-negative"
        );
    }
}

std::string_view
UrgencySlackPriorityStrategy::name() const noexcept {
    return "urgency-slack";
}

PriorityScore UrgencySlackPriorityStrategy::evaluate(
    const Task& task,
    TimePoint now
) const {
    PriorityScore result;

    // Phần điểm thứ nhất: độ quan trọng do người dùng đặt.
    const double importance = importanceValue(task.priority);
    result.score = weights_.importance * importance;
    result.reasons.push_back(
        "importance=" + std::to_string(importance)
    );

    // Không có deadline thì không thể tính urgency và slack theo deadline.
    if (!task.deadline.has_value()) {
        result.reasons.push_back(
            "no deadline: urgency and slack are neutral"
        );
        return result;
    }

    // Ví dụ: deadline 15:00, now 12:00 => còn 180 phút.
    result.timeUntilDeadline =
        std::chrono::duration_cast<std::chrono::minutes>(
            *task.deadline - now
        );

    // Slack = thời gian tới deadline - thời lượng công việc còn lại.
    // Ví dụ: còn 180 phút, Task cần 120 phút => slack = 60 phút.
    result.slack =
        *result.timeUntilDeadline
        - std::chrono::minutes{
            std::max(task.remainingMinutes, 0)
        };

    // Phần điểm thứ hai: deadline gần hay xa.
    result.score +=
        weights_.urgency
        * urgencyValue(*result.timeUntilDeadline);

    // Phần điểm thứ ba: còn bao nhiêu thời gian dự phòng.
    result.score +=
        weights_.slack
        * slackValue(*result.slack);

    // Lưu lý do để sau này Scheduler có thể giải thích quyết định.
    result.reasons.push_back(
        "minutes until deadline="
        + std::to_string(result.timeUntilDeadline->count())
    );
    result.reasons.push_back(
        "slack minutes="
        + std::to_string(result.slack->count())
    );

    return result;
}

} // namespace edu_schedule
