
#pragma once

#include "../agent/Tool.h"
#include "../models/Task.h"

class TaskLookupTool : public ITool {
private:
    const Task& task;

public:
    explicit TaskLookupTool(const Task& task);

    std::string name() const override;

    std::string description() const override;

    std::string execute(
        const std::string& input
    ) override;
};
