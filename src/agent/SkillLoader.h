
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace edu_schedule {

struct Skill {
    std::string name;
    std::string content;
    std::filesystem::path source;
};

class SkillLoader {
public:
    explicit SkillLoader(
        std::filesystem::path skillDirectory
    );

    [[nodiscard]]
    std::optional<Skill> load(
        std::string_view filename
    ) const;

    [[nodiscard]]
    std::unordered_map<std::string, Skill> loadAll() const;

private:
    std::filesystem::path skillDirectory_;
};

} // namespace edu_schedule
