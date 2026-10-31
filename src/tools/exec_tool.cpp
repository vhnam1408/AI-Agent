#include "exec_tool.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace edu_schedule {
namespace {

using json = nlohmann::json;
constexpr std::size_t kMaxListedEntries = 100;

ToolResult listFiles(const IEnvironment& environment, const json& request) {
    const auto root = environment.getWorkingDir();
    const auto relative = request.value("path", std::string("."));
    const std::filesystem::path requestedPath(relative);
    const auto target = requestedPath.is_absolute() ? requestedPath : root / requestedPath;

    if (!environment.isPathAllowed(target)) {
        return {false, "Path is outside the sandbox."};
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(target, ec) || ec) {
        return {false, "Requested path is not an accessible directory."};
    }

    std::ostringstream output;
    std::size_t count = 0;
    for (std::filesystem::directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
        if (count == kMaxListedEntries) {
            output << "... (listing capped at " << kMaxListedEntries << " entries)\n";
            break;
        }
        output << it->path().filename().string() << '\n';
        ++count;
    }
    if (ec) {
        return {false, "Could not list directory: " + ec.message()};
    }
    return {true, output.str()};
}

} // namespace

ExecTool::ExecTool(std::shared_ptr<const IEnvironment> environment)
    : environment_(std::move(environment)) {
    if (!environment_) {
        throw std::invalid_argument("ExecTool requires an environment");
    }
}

std::string_view ExecTool::name() const noexcept { return "exec"; }

std::string_view ExecTool::description() const noexcept {
    return "Runs allowlisted sandbox operations: workspace_info or list_files.";
}

ToolResult ExecTool::execute(std::string_view input) {
    if (!environment_->isSandbox()) {
        return {false, "ExecTool is disabled outside SandboxEnvironment."};
    }

    try {
        const auto request = json::parse(input);
        if (!request.is_object() || !request.contains("command")
            || !request["command"].is_string()) {
            return {false, "Expected JSON object with a string command."};
        }

        const auto command = request["command"].get<std::string>();
        if (command == "workspace_info") {
            return {true, environment_->getWorkingDir().string()};
        }
        if (command == "list_files") {
            if (request.contains("path") && !request["path"].is_string()) {
                return {false, "path must be a string."};
            }
            return listFiles(*environment_, request);
        }
        return {false, "Command is not allowlisted: " + command};
    } catch (const json::exception& error) {
        return {false, std::string("Invalid request: ") + error.what()};
    } catch (const std::filesystem::filesystem_error& error) {
        return {false, std::string("Filesystem error: ") + error.what()};
    }
}

} // namespace edu_schedule
