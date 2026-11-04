#pragma once

#include "client/llm_client.h"
#include "models/domain_types.h"
#include <nlohmann/json.hpp>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

enum class ExtractedEventKind { Submission, Exam, Class, Assigned, Other };
enum class ExtractionValidity { Valid, NeedsReview, Invalid };
enum class ExtractionErrorCode {
    InvalidInput,
    LLMFailure,
    InvalidJson,
    InvalidSchema,
    TruncatedResponse
};

// Giữ nguyên văn nguồn và ảnh Base64 đã được helper TASK-013 mã hóa ở tầng gọi.
struct ExtractionSource {
    std::string id;
    std::string text;
    std::vector<std::string> base64_images;
};

struct ExtractionRequest {
    ExtractionSource source;
    // Chỉ đặt khi bên gọi có ngữ cảnh năm rõ ràng; không lấy năm từ đồng hồ hệ thống.
    std::optional<int> reference_year;
    std::string default_timezone = "Asia/Ho_Chi_Minh";
};

struct ExtractionIssue {
    std::string code;
    std::string field;
    std::string message;
};

// Thành phần chưa biết giữ null; không dùng 00:00 hoặc năm hiện tại làm mặc định.
struct ExtractedLocalDateTime {
    std::optional<int> year;
    std::optional<int> month;
    std::optional<int> day;
    std::optional<int> hour;
    std::optional<int> minute;
    std::optional<int> second;
    std::optional<int> weekday;
    std::optional<int> utc_offset_minutes;
    std::optional<std::string> local_date;
    std::string year_origin; // source, explicit_context hoặc unresolved.
    std::string timezone;
};

struct ExtractedWorkItem {
    ExtractedEventKind kind = ExtractedEventKind::Other;
    std::optional<std::string> course;
    std::optional<std::string> title;
    std::optional<std::string> date_text;
    std::optional<std::string> location;
    std::string source_id;
    std::string evidence;
    ExtractedLocalDateTime when;
    // event_time: thời điểm sự kiện. deadline chỉ được đặt cho Submission hợp lệ.
    std::optional<TimePoint> event_time;
    std::optional<TimePoint> deadline;
    std::vector<std::string> missing_fields;
    std::vector<ExtractionIssue> issues;
    ExtractionValidity validity = ExtractionValidity::Valid;
};

struct DeadlineExtraction {
    ExtractionSource source;
    std::optional<int> reference_year;
    std::string default_timezone;
    std::vector<ExtractedWorkItem> items;
    std::string raw_model_content;
    std::optional<LLMResponse> response_metadata;
};

struct ExtractionError {
    ExtractionErrorCode code;
    std::string message;
    std::string raw_model_content;
    std::optional<LLMError> llm_error;
};
using DeadlineExtractionResult = std::expected<DeadlineExtraction, ExtractionError>;

class DeadlineExtractor {
public:
    // Mượn interface Client; bên gọi giữ Client sống lâu hơn extractor.
    explicit DeadlineExtractor(LLMClient& client) noexcept : client_(client) {
    }

    // Gửi text/ảnh qua LLMClient, parse JSON dữ liệu, kiểm tra bằng chứng và chuẩn hóa ngày.
    [[nodiscard]] DeadlineExtractionResult extract(const ExtractionRequest& request);

    // Parse dữ liệu đã có để replay/kiểm thử độc lập; không nhận tool-call envelope.
    [[nodiscard]] static DeadlineExtractionResult parse_response(std::string_view content,
                                                                 const ExtractionRequest& request);

    // Kiểm thành phần ngày giờ tường minh bằng validator TASK-017; không đọc nguồn/LLM.
    // Trả item Other chỉ có event_time khi đủ dữ liệu; không tự cấp deadline/năm/giờ.
    [[nodiscard]] static ExtractedWorkItem validate_date_time(const ExtractedLocalDateTime& when);

    // Dùng cùng quy tắc TASK-017 để nhận diện cùng công việc; không tự tạo TaskId.
    [[nodiscard]] static bool same_event(const ExtractedWorkItem& first,
                                         const ExtractedWorkItem& second);

private:
    LLMClient& client_;
};

// Xuất dữ liệu có cấu trúc; tasks chỉ gồm Submission, vẫn giữ trạng thái/issue của từng mục.
// Ảnh gốc được giữ trong result.source; JSON xuất image_count để tránh lặp megabyte Base64.
[[nodiscard]] nlohmann::json deadline_extraction_to_json(const DeadlineExtraction& result);
[[nodiscard]] nlohmann::json deadline_extraction_error_to_json(const ExtractionError& error);

} // namespace edu_schedule
