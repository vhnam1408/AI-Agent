#pragma once

#include "agent/deadline_extractor.h"
#include <cstddef>
#include <set>

namespace edu_schedule {

enum class DeadlineConfirmationStatus { NotRequired, Pending, Confirmed, NotApplicable };

// Định danh gồm source + vị trí trong snapshot bất biến; revision chống trả lời cũ.
struct DeadlineConfirmationRequest {
    std::string source_id;
    std::size_t item_index = 0;
    std::size_t revision = 0;
    DeadlineConfirmationStatus status = DeadlineConfirmationStatus::Pending;
    std::vector<std::string> required_fields;
    std::vector<ExtractionIssue> issues;
    std::vector<std::string> questions;
};

// Adapter UI chỉ đặt explicit_confirmation sau thao tác xác nhận của người dùng.
// Các optional vắng mặt nghĩa là chưa bổ sung, không phải xóa dữ liệu nguồn.
// Múi giờ bổ sung dùng offset UTC phút tường minh; không nhận tên vùng/DST suy đoán.
struct DeadlineConfirmationAnswer {
    std::string source_id;
    std::size_t item_index = 0;
    std::size_t revision = 0;
    bool explicit_confirmation = false;
    ExtractedLocalDateTime when;
    std::optional<std::string> course;
    std::optional<std::string> title;
    bool evidence_verified = false;
    bool resolve_conflicts = false;
    bool submission_verified = false;
};

// Phiên xác nhận thuần dữ liệu, không gọi LLM, Scheduler hoặc repository.
// Giữ snapshot trích xuất bất biến và lịch sử câu trả lời riêng để truy vết.
class DeadlineConfirmation {
public:
    // Nhận kết quả parse/extract TASK-017; kiểm lại raw JSON để tránh tin cache bị sửa.
    // Ném invalid_argument nếu snapshot không replay được, out_of_range nếu sai index.
    DeadlineConfirmation(const DeadlineExtraction& extraction, std::size_t item_index);

    // Trả yêu cầu hiện tại; Pending không bao giờ đi kèm deadline dùng được.
    [[nodiscard]] DeadlineConfirmationRequest request() const;

    // Chỉ áp dụng câu trả lời khớp source/index/revision và xác nhận tường minh.
    // Sai định danh/thiếu xác nhận trả lỗi, không đổi phiên; dữ liệu sai vẫn Pending.
    [[nodiscard]] std::expected<DeadlineConfirmationRequest, std::string>
    answer(const DeadlineConfirmationAnswer& answer);

    // Chỉ trả deadline cho Submission đã hợp lệ từ nguồn hoặc được xác nhận đủ.
    [[nodiscard]] std::optional<TimePoint> deadline() const noexcept;

    // Trả snapshot gốc, gồm evidence/raw/ảnh và validity ban đầu; không sửa sau xác nhận.
    [[nodiscard]] const DeadlineExtraction& original() const noexcept;

    // Trả lịch sử dữ liệu tường minh đã nhận; không trộn vào evidence trích xuất.
    [[nodiscard]] const std::vector<DeadlineConfirmationAnswer>& answers() const noexcept;

    // Trả thành phần ngày giờ đang xét để hiển thị; không lộ TimePoint chưa được duyệt.
    [[nodiscard]] const ExtractedLocalDateTime& date_time() const noexcept;

private:
    // Tính lại yêu cầu từ missing_fields/issues gốc và validator chung sau mỗi câu trả lời.
    void refresh();

    DeadlineExtraction original_;
    std::size_t index_;
    ExtractedLocalDateTime when_;
    std::optional<std::string> course_;
    std::optional<std::string> title_;
    std::set<std::string> supplied_;
    bool evidence_verified_ = false;
    bool conflicts_resolved_ = false;
    bool submission_verified_ = false;
    std::vector<DeadlineConfirmationAnswer> answers_;
    ExtractedWorkItem validated_;
    DeadlineConfirmationRequest request_;
    std::optional<TimePoint> deadline_;
};

} // namespace edu_schedule
