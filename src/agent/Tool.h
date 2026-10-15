
#pragma once

#include <string>

class ITool {
public:
    virtual ~ITool() = default;

    virtual std::string name() const = 0;

    virtual std::string description() const = 0;

    virtual std::string execute(
        const std::string& input
    ) = 0;
};
