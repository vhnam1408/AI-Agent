#pragma once

#include <string>
#include <deque>
#include <cstdint>
#include <string_view>

namespace edu_schedule {

// Kiểu lặp vô tận
enum class LoopType : uint8_t {
    None,
    GenericRepeat,  // Lặp liên tiếp cùng 1 hành động: A -> A -> A
    PingPong        // Lặp qua lại giữa 2 hành động: A -> B -> A -> B
};

// Mức độ nghiêm trọng của lỗi lặp
enum class LoopSeverity : uint8_t {
    None,
    Warning,        // Cảnh báo: AI nên đổi chiến thuật khác
    Critical        // Nghiêm trọng: Buộc dừng ngay để tránh tốn tiền và tài nguyên
};

// Kết quả sau khi kiểm tra lặp
struct LoopCheckResult {
    LoopSeverity severity{LoopSeverity::None};
    LoopType     type{LoopType::None};
    int          count{0};
    std::string  message;

    // Kiểm tra xem có phát hiện lặp hay không
    bool isLoopDetected() const noexcept { return severity != LoopSeverity::None; }
    
    // Kiểm tra xem có đến mức độ nguy hiểm cần dừng khẩn cấp không
    bool isCritical() const noexcept { return severity == LoopSeverity::Critical; }
    
    // Kiểm tra xem có phải chỉ là cảnh báo nhẹ không
    bool isWarning() const noexcept { return severity == LoopSeverity::Warning; }
};

class LoopDetector {
public:
    struct Config {
        size_t warning_threshold  = 2;  // Lặp 2 lần thì cảnh báo
        size_t critical_threshold = 4;  // Lặp 4 lần thì dừng khẩn cấp
        size_t history_capacity   = 30; // Bộ nhớ lưu tối đa 30 hành động gần nhất
    };

    LoopDetector() = default;
    explicit LoopDetector(Config config) : config_(config) {}

    // Ghi nhận một hành động vừa xảy ra vào lịch sử
    void recordAction(const std::string& action_signature) {
        history_.push_back(action_signature);
        if (history_.size() > config_.history_capacity) {
            history_.pop_front();
        }
    }

    // Quét toàn bộ lịch sử để tìm xem có bị lặp A->A->A hoặc A->B->A->B không
    LoopCheckResult checkLoop() const {
        if (history_.size() < 2) return {};

        // 1. Kiểm tra lặp cùng một hành động liên tiếp (Generic Repeat)
        size_t repeat_count = 1;
        const auto& last_action = history_.back();
        for (auto it = history_.rbegin() + 1; it != history_.rend(); ++it) {
            if (*it == last_action) repeat_count++;
            else break;
        }

        if (repeat_count >= config_.critical_threshold) {
            return { LoopSeverity::Critical, LoopType::GenericRepeat, (int)repeat_count,
                     "Dung khan cap: Hanh dong lap lien tiep qua nguong cho phep!" };
        }
        if (repeat_count >= config_.warning_threshold) {
            return { LoopSeverity::Warning, LoopType::GenericRepeat, (int)repeat_count,
                     "Canh bao: Hanh dong dang bi lap lai!" };
        }

        // 2. Kiểm tra lặp bóng bàn luẩn quẩn giữa 2 hành động (Ping-Pong)
        if (history_.size() >= 4) {
            const auto& a = history_.back();
            const auto& b = *(history_.rbegin() + 1);
            if (a != b) {
                size_t pairs = 1;
                for (size_t i = 2; i + 1 < history_.size(); i += 2) {
                    if (*(history_.rbegin() + i) == a && *(history_.rbegin() + i + 1) == b) pairs++;
                    else break;
                }

                if (pairs >= config_.critical_threshold / 2 + 1) {
                    return { LoopSeverity::Critical, LoopType::PingPong, (int)pairs,
                             "Dung khan cap: Lap qua lai giua 2 hanh dong qua nhieu!" };
                }
                if (pairs >= 2) {
                    return { LoopSeverity::Warning, LoopType::PingPong, (int)pairs,
                             "Canh bao: Dang bi lap qua lai giua 2 hanh dong!" };
                }
            }
        }

        return {};
    }

    // Tiện ích: Vừa lưu hành động vừa kiểm tra lặp ngay lập tức
    LoopCheckResult recordAndCheck(const std::string& action_signature) {
        recordAction(action_signature);
        return checkLoop();
    }

    // Xóa sạch lịch sử để tái sử dụng cho lượt chạy mới
    void reset() noexcept { history_.clear(); }

private:
    Config config_;
    std::deque<std::string> history_;
};

} // namespace edu_schedule
