#include "agent/deadline_confirmation.h"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace edu_schedule {
namespace {

// Chuỗi metadata chỉ được chấp nhận khi có nội dung; không tự bổ sung nhãn môn/việc.
bool nonblank(const std::optional<std::string>& value) {
    return value && value->find_first_not_of(" \t\r\n") != std::string::npos;
}

struct EvidenceContext {
    std::string text;
    bool truncated = false;
};

// Đọc trọn các dòng chứa quote, kể cả quote lặp; không mượn dữ liệu từ dòng khác.
// Phần nguồn bị cắt có nội dung phải được người dùng đối chiếu và xác nhận lại.
EvidenceContext evidence_context(const ExtractionSource& source, const std::string& evidence) {
    EvidenceContext context{evidence, false};
    std::size_t position = source.text.find(evidence);
    while (!evidence.empty() && position != std::string::npos) {
        const auto previous = position == 0 ? std::string::npos
            : source.text.find_last_of("\r\n", position - 1);
        const auto begin = previous == std::string::npos ? 0 : previous + 1;
        const auto quote_end = position + evidence.size();
        const auto next = source.text.find_first_of("\r\n", quote_end);
        const auto end = next == std::string::npos ? source.text.size() : next;
        context.text += "\n" + source.text.substr(begin, end - begin);
        context.truncated = context.truncated ||
            source.text.substr(begin, position - begin).find_first_not_of(" \t\r\n") != std::string::npos ||
            source.text.substr(quote_end, end - quote_end).find_first_not_of(" \t\r\n") != std::string::npos;
        position = source.text.find(evidence, position + 1);
    }
    return context;
}

// Giữ thứ tự deterministic và tránh hỏi lặp cùng một trường.
void require_field(DeadlineConfirmationRequest& request, const std::string& field) {
    if (std::find(request.required_fields.begin(), request.required_fields.end(), field) ==
        request.required_fields.end()) {
        request.required_fields.push_back(field);
    }
}

// Chỉ coi nhóm trường đã được xác nhận nếu từng thành phần được gửi tường minh.
bool supplied_all(const std::set<std::string>& fields,
                  std::initializer_list<std::string_view> required) {
    return std::all_of(required.begin(), required.end(), [&](std::string_view field) {
        return fields.contains(std::string(field));
    });
}

// Câu hỏi theo field, không gọi model và không gợi ý một deadline mặc định.
std::string question_for(const std::string& field) {
    if (field == "year") return "Bạn xác nhận deadline thuộc năm nào (đủ bốn chữ số)?";
    if (field == "date") return "Bạn xác nhận đầy đủ năm, tháng và ngày của deadline?";
    if (field == "time") return "Bạn xác nhận giờ và phút của deadline?";
    if (field == "timezone") return "Bạn xác nhận UTC offset theo phút của deadline?";
    if (field == "weekday") return "Bạn xác nhận lại thứ ISO (1=thứ hai, 7=chủ nhật) và ngày tương ứng?";
    if (field == "evidence") return "Bạn đã đối chiếu bằng chứng gốc và xác minh thông tin trích xuất, kể cả ảnh?";
    if (field == "conflicts") return "Có nhiều deadline mâu thuẫn. Bạn xác nhận thời điểm đầy đủ và lựa chọn áp dụng cho sự kiện này?";
    if (field == "kind") return "Bạn xác nhận nguồn này thực sự ghi hạn nộp bài?";
    if (field == "course") return "Bạn xác nhận tên môn học?";
    if (field == "title") return "Bạn xác nhận tên công việc cần nộp?";
    return "Cần làm rõ trường '" + field + "'; chưa thể cấp deadline.";
}

// Giữ lý do chưa giải quyết và field cần hỏi; không đổi validity của item gốc.
void retain_issue(DeadlineConfirmationRequest& request, const ExtractionIssue& issue,
                  const std::string& field) {
    request.issues.push_back(issue);
    require_field(request, field);
}

} // namespace

// Replay contract cũ bằng API public, giữ response metadata và nguyên nguồn ảnh/text.
DeadlineConfirmation::DeadlineConfirmation(const DeadlineExtraction& extraction,
                                           std::size_t item_index)
    : index_(item_index) {
    ExtractionRequest input{extraction.source, extraction.reference_year,
                            extraction.default_timezone};
    auto replay = DeadlineExtractor::parse_response(extraction.raw_model_content, input);
    if (!replay) {
        throw std::invalid_argument("Confirmation requires replayable TASK-017 extraction");
    }
    original_ = std::move(*replay);
    original_.response_metadata = extraction.response_metadata;
    if (index_ >= original_.items.size()) {
        throw std::out_of_range("Confirmation item index outside extraction snapshot");
    }
    const auto& item = original_.items[index_];
    when_ = item.when;
    course_ = item.course;
    title_ = item.title;
    refresh();
}

// Getter trả bản sao request để UI không thể sửa trạng thái nội bộ.
DeadlineConfirmationRequest DeadlineConfirmation::request() const {
    return request_;
}

// Patch chỉ nhận thành phần được khai báo; local_date/year_origin/timezone gửi lên bị bỏ qua.
std::expected<DeadlineConfirmationRequest, std::string>
DeadlineConfirmation::answer(const DeadlineConfirmationAnswer& answer) {
    if (answer.source_id != request_.source_id || answer.item_index != index_ ||
        answer.revision != request_.revision) {
        return std::unexpected("Source/item/revision mismatch");
    }
    if (!answer.explicit_confirmation) {
        return std::unexpected("Explicit user confirmation required");
    }
    if (request_.status == DeadlineConfirmationStatus::NotApplicable) {
        return std::unexpected("Only submission items can receive a deadline");
    }
    // Chỉ optional có giá trị mới ghi vào candidate; số 0 vẫn là giá trị tường minh.
    const auto apply = [&](const std::optional<int>& value, std::optional<int>& target,
                           const std::string& field) {
        if (value) {
            target = value;
            supplied_.insert(field);
        }
    };
    apply(answer.when.year, when_.year, "year");
    apply(answer.when.month, when_.month, "month");
    apply(answer.when.day, when_.day, "day");
    apply(answer.when.hour, when_.hour, "hour");
    apply(answer.when.minute, when_.minute, "minute");
    apply(answer.when.second, when_.second, "second");
    apply(answer.when.weekday, when_.weekday, "weekday");
    apply(answer.when.utc_offset_minutes, when_.utc_offset_minutes, "timezone");
    if (answer.when.utc_offset_minutes) when_.timezone = "user_confirmed_offset";
    if (answer.when.year) when_.year_origin = "user_confirmed";
    if (answer.course) course_ = answer.course;
    if (answer.title) title_ = answer.title;
    evidence_verified_ = evidence_verified_ || answer.evidence_verified;
    conflicts_resolved_ = conflicts_resolved_ || answer.resolve_conflicts;
    submission_verified_ = submission_verified_ || answer.submission_verified;
    answers_.push_back(answer);
    refresh();
    return request_;
}

// Chỉ nguồn đủ tin cậy hoặc xác nhận đủ điều kiện mới có deadline_ sau refresh.
std::optional<TimePoint> DeadlineConfirmation::deadline() const noexcept {
    return deadline_;
}

// Snapshot trích xuất bất biến dành cho audit và hiển thị bằng chứng.
const DeadlineExtraction& DeadlineConfirmation::original() const noexcept {
    return original_;
}

// Lịch sử giữ từng câu trả lời được nhận, kể cả câu trả lời chưa đủ hoặc sai ngày.
const std::vector<DeadlineConfirmationAnswer>& DeadlineConfirmation::answers() const noexcept {
    return answers_;
}

// Chỉ hiển thị thành phần candidate; TimePoint được bảo vệ bởi cổng deadline().
const ExtractedLocalDateTime& DeadlineConfirmation::date_time() const noexcept {
    return validated_.when;
}

// Validator chung kiểm lịch/UTC; policy ở đây chỉ xét provenance và lựa chọn tường minh.
void DeadlineConfirmation::refresh() {
    const auto& item = original_.items[index_];
    request_ = {};
    request_.source_id = original_.source.id;
    request_.item_index = index_;
    request_.revision = answers_.size();
    deadline_.reset();
    validated_ = DeadlineExtractor::validate_date_time(when_);
    if (item.kind != ExtractedEventKind::Submission) {
        request_.status = DeadlineConfirmationStatus::NotApplicable;
        return;
    }
    request_.issues = validated_.issues;
    for (const auto& field : validated_.missing_fields) require_field(request_, field);
    for (const auto& issue : validated_.issues) require_field(request_, issue.field);
    if (!nonblank(course_)) {
        retain_issue(request_, {"missing_field", "course", "Course requires confirmation"}, "course");
    }
    if (!nonblank(title_)) {
        retain_issue(request_, {"missing_field", "title", "Title requires confirmation"}, "title");
    }
    const bool date_supplied = supplied_all(supplied_, {"year", "month", "day"});
    const bool time_supplied = supplied_all(supplied_, {"hour", "minute"});
    const auto context = evidence_context(original_.source, item.evidence);
    if (context.truncated && !(evidence_verified_ && date_supplied && time_supplied)) {
        retain_issue(request_, {"incomplete_source_evidence", "evidence",
                               "Quote omits source context; verify source and confirm full timestamp"}, "evidence");
        if (!date_supplied) require_field(request_, "date");
        if (!time_supplied) require_field(request_, "time");
    }
    // Nhiều occurrence chưa đủ chuẩn hóa: không kết luận xung đột giả, cũng không chọn hộ.
    // Cả hai TimePoint đã hợp lệ thì parser TASK-017 đã so UTC và xử lý conflict.
    for (std::size_t other_index = 0; other_index < original_.items.size(); ++other_index) {
        const auto& other = original_.items[other_index];
        if (other_index != index_ && DeadlineExtractor::same_event(item, other) &&
            item.date_text != other.date_text && (!item.event_time || !other.event_time) &&
            !(date_supplied && time_supplied && conflicts_resolved_)) {
            retain_issue(request_, {"unresolved_occurrences", "conflicts",
                                   "Multiple source occurrences require explicit user selection"}, "conflicts");
            require_field(request_, "date");
            require_field(request_, "time");
            break;
        }
    }
    // Đọc cả dòng nguồn: model cắt quote không được làm mất lựa chọn thời gian tương đối.
    for (const auto term : {"tuần sau", "Tuần sau", "cuối tháng", "Cuối tháng", "ngày mai",
                            "Ngày mai", "tuần tới", "tháng sau", "next week", "Next week",
                            "tomorrow", "end of month"}) {
        if (context.text.find(term) != std::string::npos) {
            if (!date_supplied || !time_supplied) {
                retain_issue(request_, {"ambiguous_relative_time", "date",
                                       "Relative deadline requires an explicit absolute timestamp"}, "date");
                require_field(request_, "time");
            }
            break;
        }
    }
    for (const auto& issue : item.issues) {
        const auto& code = issue.code;
        bool resolved = false;
        std::string field = issue.field;
        if (code == "missing_field") {
            resolved = field == "date" || field == "year" || field == "time" ||
                       field == "timezone" || field == "course" || field == "title";
        } else if (code == "invalid_date" || code == "invalid_time" ||
                   code == "weekday_mismatch") {
            // Candidate giữ nguyên thành phần sai cho tới khi người dùng sửa; validator kiểm lại.
            resolved = true;
        } else if (code == "invalid_timezone" || code == "unsupported_timezone") {
            // Parser có thể còn offset mặc định sau khi báo lỗi nguồn; không dùng fallback đó.
            resolved = supplied_.contains("timezone");
        } else if (code == "ambiguous_year") {
            resolved = supplied_.contains("year");
        } else if (code == "invalid_weekday" || code == "ambiguous_weekday") {
            resolved = supplied_.contains("weekday");
        } else if (code == "conflicting_dates") {
            resolved = date_supplied;
        } else if (code == "conflicting_times" || code == "unsupported_time_precision" ||
                   code == "omitted_source_time") {
            resolved = time_supplied;
            field = "time";
        } else if (code == "conflicting_timezones") {
            resolved = supplied_.contains("timezone");
        } else if (code == "conflicting_items") {
            resolved = date_supplied && time_supplied && conflicts_resolved_;
            field = "conflicts";
            if (!resolved) {
                require_field(request_, "date");
                require_field(request_, "time");
            }
        } else if (code == "ungrounded_evidence" || code == "ungrounded_field" ||
                   code == "unverified_visual_evidence") {
            resolved = evidence_verified_;
            field = "evidence";
        } else if (code == "unsupported_submission") {
            resolved = submission_verified_;
            field = "kind";
        }
        if (!resolved) retain_issue(request_, issue, field);
    }
    // Field/code mới trong TASK-017 phải được phối hợp policy, không tự bỏ qua.
    for (const auto& field : item.missing_fields) {
        if (field != "date" && field != "year" && field != "time" &&
            field != "timezone" && field != "course" && field != "title") {
            retain_issue(request_, {"unhandled_missing_field", field,
                                   "Confirmation policy requires integration update"}, field);
        }
    }
    if (request_.issues.empty() && validated_.event_time) {
        request_.status = answers_.empty() ? DeadlineConfirmationStatus::NotRequired
                                          : DeadlineConfirmationStatus::Confirmed;
        deadline_ = validated_.event_time;
    } else {
        request_.status = DeadlineConfirmationStatus::Pending;
        for (const auto& field : request_.required_fields) {
            request_.questions.push_back(question_for(field));
        }
    }
}

} // namespace edu_schedule
