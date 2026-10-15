
#pragma once

#include <string>
#include "Priority.h"

struct Course {
    std::string id;
    std::string name;
    Priority priority = Priority::Mid;
};
