#include "Course.h"

namespace edu_schedule {

bool Course::isValid() const noexcept {
    return !id.empty() && !name.empty();
}

} // namespace edu_schedule
