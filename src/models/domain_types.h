#pragma once

#include <chrono>
#include <string>

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - Các kiểu dùng chung của miền dữ liệu chỉ được định nghĩa tại header này.
// - TimePoint là thời điểm tuyệt đối; tầng nhập/xuất chịu trách nhiệm đổi múi giờ.
// - Không định nghĩa lại TimePoint hoặc TaskId trong header khác.
using TimePoint = std::chrono::system_clock::time_point;
using TaskId = std::string;

} // namespace edu_schedule
