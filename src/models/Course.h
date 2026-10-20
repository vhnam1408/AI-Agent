
#pragma once

#include <string>
#include "priority.h"

namespace edu_schedule {

// QUY ƯỚC BẮT BUỘC:
// - id ổn định và duy nhất; Task chỉ tham chiếu bằng courseId.
// - Course không sở hữu Task.
// - Priority của Course là gợi ý mặc định, không âm thầm ghi đè Task đã có.
struct Course {
    std::string id;
    std::string name;
    Priority priority = Priority::Mid;
};

} // namespace edu_schedule
