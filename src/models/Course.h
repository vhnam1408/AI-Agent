#pragma once

#include <string>
#include "priority.h"

namespace edu_schedule {

// Course does not own Tasks; Tasks refer to a Course by courseId.
struct Course {
    std::string id;
    std::string name;
    Priority priority = Priority::Mid;

    [[nodiscard]] bool isValid() const noexcept;
};

} // namespace edu_schedule
