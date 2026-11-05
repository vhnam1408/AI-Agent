#include "task_decomposer.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace edu_schedule {
namespace {

using json = nlohmann::json;

// Chuyển chuỗi định danh mẫu sang enum DecompositionPattern
DecompositionPattern parsePattern(std::string_view name) {
    if (name == "lab" || name == "lab_assignment") {
        return DecompositionPattern::LabAssignment;
    }
    if (name == "essay" || name == "research_essay" || name == "theory") {
        return DecompositionPattern::ResearchEssay;
    }
    if (name == "custom") {
        return DecompositionPattern::Custom;
    }
    return DecompositionPattern::SoftwareProject;
}

std::string patternToString(DecompositionPattern pattern) {
    switch (pattern) {
    case DecompositionPattern::SoftwareProject:
        return "software_project";
    case DecompositionPattern::LabAssignment:
        return "lab_assignment";
    case DecompositionPattern::ResearchEssay:
        return "research_essay";
    case DecompositionPattern::Custom:
        return "custom";
    }
    return "software_project";
}

} // namespace

// ============================================================================
// WorkspaceBootstrapper
// ============================================================================

WorkspaceBootstrapper::WorkspaceBootstrapper(std::shared_ptr<const IEnvironment> environment)
    : environment_(std::move(environment)) {}

bool WorkspaceBootstrapper::writeTemplateFile(const std::filesystem::path& targetPath,
                                              std::string_view content) const {
    if (!environment_ || !environment_->isPathAllowed(targetPath)) {
        return false;
    }

    std::error_code ec;
    const auto parentDir = targetPath.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        std::filesystem::create_directories(parentDir, ec);
        if (ec) return false;
    }

    std::ofstream out(targetPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    out << content;
    return out.good();
}

std::string WorkspaceBootstrapper::generateReadme(const WorkspaceConfig& config) const {
    std::ostringstream ss;
    ss << "# Workspace: " << (config.title.empty() ? config.taskId : config.title) << "\n\n";
    ss << "- **Mã môn học:** " << (config.courseId.empty() ? "N/A" : config.courseId) << "\n";
    ss << "- **Mã nhiệm vụ cha:** `" << config.taskId << "`\n";
    ss << "- **Tổng số bước con:** " << config.subtasks.size() << "\n\n";

    ss << "## 1. Danh sách các giai đoạn & Nhiệm vụ con\n\n";
    ss << "| STT | Mã bước | Tên nhiệm vụ | Thời lượng (phút) | Trạng thái | Phụ thuộc |\n";
    ss << "| :---: | :--- | :--- | :---: | :---: | :--- |\n";

    for (std::size_t i = 0; i < config.subtasks.size(); ++i) {
        const auto& t = config.subtasks[i];
        ss << "| " << (i + 1) << " | `" << t.id << "` | " << t.title << " | "
           << t.estimatedMinutes << " | Todo | ";
        if (t.dependencies.empty()) {
            ss << "*(Khởi đầu)*";
        } else {
            for (std::size_t d = 0; d < t.dependencies.size(); ++d) {
                if (d > 0) ss << ", ";
                ss << "`" << t.dependencies[d] << "`";
            }
        }
        ss << " |\n";
    }

    ss << "\n## 2. Danh mục tài liệu học tập\n\n";
    ss << "- Đặc tả yêu cầu & DoD: [`docs/requirements.md`](docs/requirements.md)\n";
    ss << "- Tài liệu tham khảo: [`docs/references.md`](docs/references.md)\n";
    ss << "- Mã nguồn bài làm: [`src/main.cpp`](src/main.cpp)\n";
    ss << "- Báo cáo tổng kết: [`reports/final_report.md`](reports/final_report.md)\n";

    return ss.str();
}

std::string WorkspaceBootstrapper::generateRequirements(const WorkspaceConfig& config) const {
    std::ostringstream ss;
    ss << "# Đặc tả yêu cầu & Tiêu chí hoàn thành (DoD)\n\n";
    ss << "## Nhiệm vụ: " << (config.title.empty() ? config.taskId : config.title) << "\n\n";
    ss << "### 1. Mục tiêu học tập\n";
    ss << "- Nắm vững kiến thức trọng tâm môn " << config.courseId << ".\n";
    ss << "- Hoàn thành đầy đủ các giai đoạn theo kế hoạch đã phân rã.\n\n";
    ss << "### 2. Tiêu chí hoàn thành (Definition of Done - DoD)\n";
    for (const auto& t : config.subtasks) {
        ss << "- [ ] **" << t.title << "**: Mã nguồn kiểm thử đạt yêu cầu, không còn lỗi biên dịch.\n";
    }
    ss << "\n### 3. Quy chuẩn nộp bài\n";
    ss << "- Nộp mã nguồn sạch, có chú thích rõ ràng.\n";
    ss << "- Đi kèm báo cáo đánh giá kết quả tại `reports/final_report.md`.\n";
    return ss.str();
}

std::string WorkspaceBootstrapper::generateReferences(const WorkspaceConfig& config) const {
    std::ostringstream ss;
    ss << "# Tài liệu tham khảo đã kiểm chứng\n\n";
    ss << "Môn học: " << config.courseId << " | Nhiệm vụ: " << config.taskId << "\n\n";
    if (config.referenceLinks.empty()) {
        ss << "*Chưa có liên kết tài liệu nào được đính kèm. Dùng WebSearchTool để bổ sung nguồn tin cậy.*\n";
    } else {
        for (const auto& link : config.referenceLinks) {
            ss << "- [" << link << "](" << link << ")\n";
        }
    }
    return ss.str();
}

std::string WorkspaceBootstrapper::generateStarterCode(const WorkspaceConfig& config) const {
    std::ostringstream ss;
    ss << "// Auto-generated starter template for " << config.taskId << "\n";
    ss << "// Course: " << config.courseId << "\n\n";
    ss << "#include <iostream>\n\n";
    ss << "int main() {\n";
    ss << "    std::cout << \"Ready to work on " << config.taskId << "!\\n\";\n";
    ss << "    return 0;\n";
    ss << "}\n";
    return ss.str();
}

std::string WorkspaceBootstrapper::generateReportTemplate(const WorkspaceConfig& config) const {
    std::ostringstream ss;
    ss << "# Báo cáo bài tập / Dự án: " << (config.title.empty() ? config.taskId : config.title) << "\n\n";
    ss << "- **Học phần:** " << config.courseId << "\n";
    ss << "- **Mã bài tập:** " << config.taskId << "\n\n";
    ss << "## 1. Tóm tắt kết quả đạt được\n";
    ss << "*(Trình bày ngắn gọn các tính năng đã hoàn thành và điểm nổi bật)*\n\n";
    ss << "## 2. Kiến trúc giải pháp\n";
    ss << "*(Mô tả sơ đồ thiết kế lớp, luồng dữ liệu hoặc mô hình giải thuật)*\n\n";
    ss << "## 3. Kết quả kiểm thử & Nghiệm thu\n";
    ss << "*(Bảng kết quả chạy test và các chỉ số đo đạc)*\n\n";
    ss << "## 4. Kết luận & Hướng phát triển\n";
    ss << "*(Đánh giá mức độ hoàn thành so với mục tiêu ban đầu)*\n";
    return ss.str();
}

BootstrapResult WorkspaceBootstrapper::bootstrap(const WorkspaceConfig& config) const {
    if (!environment_) {
        return {false, "WorkspaceBootstrapper requires a valid Environment.", {}, {}};
    }

    if (config.taskId.empty()) {
        return {false, "Cannot bootstrap workspace: taskId is empty.", {}, {}};
    }

    // Chặn tên thư mục chứa dấu điều hướng nguy hiểm
    if (config.taskId.find("..") != std::string::npos ||
        config.courseId.find("..") != std::string::npos) {
        return {false, "Path traversal detected in taskId or courseId.", {}, {}};
    }

    const auto root = environment_->getWorkingDir();
    const auto targetDir = config.courseId.empty()
        ? root / config.taskId
        : root / config.courseId / config.taskId;

    if (!environment_->isPathAllowed(targetDir)) {
        return {false, "Target workspace path is not allowed in sandbox: " + targetDir.string(), {}, {}};
    }

    std::error_code ec;
    std::vector<std::filesystem::path> subdirs = {
        targetDir / "docs",
        targetDir / "src",
        targetDir / "tests",
        targetDir / "reports"
    };

    for (const auto& dir : subdirs) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return {false, "Failed to create directory " + dir.string() + ": " + ec.message(), targetDir, {}};
        }
    }

    std::vector<std::filesystem::path> createdFiles;
    struct TemplateItem {
        std::filesystem::path path;
        std::string content;
    };

    std::vector<TemplateItem> templates = {
        {targetDir / "README.md", generateReadme(config)},
        {targetDir / "docs" / "requirements.md", generateRequirements(config)},
        {targetDir / "docs" / "references.md", generateReferences(config)},
        {targetDir / "src" / "main.cpp", generateStarterCode(config)},
        {targetDir / "reports" / "final_report.md", generateReportTemplate(config)}
    };

    for (const auto& item : templates) {
        if (!writeTemplateFile(item.path, item.content)) {
            return {false, "Failed to write file " + item.path.string(), targetDir, createdFiles};
        }
        createdFiles.push_back(item.path);
    }

    return {true, "Successfully bootstrapped workspace with " + std::to_string(createdFiles.size()) + " templates.", targetDir, createdFiles};
}

// ============================================================================
// TaskDecomposer
// ============================================================================

TaskDecomposer::TaskDecomposer(
    std::shared_ptr<ITaskRepository> repository,
    std::shared_ptr<const IEnvironment> environment)
    : repository_(std::move(repository)),
      environment_(std::move(environment)) {
    if (environment_) {
        bootstrapper_ = std::make_unique<WorkspaceBootstrapper>(environment_);
    }
}

std::vector<TaskDecomposer::StepBlueprint> TaskDecomposer::getBlueprints(
    DecompositionPattern pattern,
    const std::vector<std::string>& customTitles) const {

    switch (pattern) {
    case DecompositionPattern::SoftwareProject:
        return {
            {"analysis", "Phân tích yêu cầu và đặc tả hệ thống", 0.15, 0.15},
            {"design", "Thiết kế kiến trúc hệ thống và biểu đồ UML", 0.20, 0.35},
            {"coding", "Cài đặt mã nguồn các chức năng cốt lõi", 0.35, 0.70},
            {"testing", "Kiểm thử đơn vị và kiểm thử tích hợp", 0.15, 0.85},
            {"report", "Hoàn thiện tài liệu và báo cáo tổng kết", 0.15, 1.00}
        };

    case DecompositionPattern::LabAssignment:
        return {
            {"prep", "Chuẩn bị môi trường và ôn tập lý thuyết", 0.20, 0.20},
            {"impl", "Cài đặt các bài tập thực hành phòng Lab", 0.40, 0.60},
            {"verify", "Kiểm thử đầu ra và đối chiếu kết quả mẫu", 0.20, 0.80},
            {"submission", "Tổng hợp kết quả và chuẩn bị nộp bài", 0.20, 1.00}
        };

    case DecompositionPattern::ResearchEssay:
        return {
            {"research", "Thu thập tài liệu và khảo cứu đề tài", 0.25, 0.25},
            {"outline", "Lập dàn ý chi tiết và phân mục nội dung", 0.20, 0.45},
            {"draft", "Viết bản thảo nội dung chính", 0.35, 0.80},
            {"review", "Rà soát trích dẫn và hiệu đính văn bản", 0.20, 1.00}
        };

    case DecompositionPattern::Custom: {
        std::vector<StepBlueprint> blueprints;
        if (customTitles.empty()) {
            return blueprints;
        }
        const double weight = 1.0 / static_cast<double>(customTitles.size());
        double cumulative = 0.0;
        for (std::size_t i = 0; i < customTitles.size(); ++i) {
            cumulative += weight;
            blueprints.push_back({
                "step_" + std::to_string(i + 1),
                customTitles[i],
                weight,
                std::min(1.0, cumulative)
            });
        }
        if (!blueprints.empty()) {
            blueprints.back().cumulativeRatio = 1.0;
        }
        return blueprints;
    }
    }

    return {};
}

DecompositionResult TaskDecomposer::decompose(const DecompositionRequest& request) {
    if (request.parentTaskId.empty()) {
        return {false, "parentTaskId cannot be empty.", {}, {}, std::nullopt};
    }
    if (request.totalEstimatedMinutes < 0) {
        return {false, "totalEstimatedMinutes cannot be negative.", {}, {}, std::nullopt};
    }

    const auto blueprints = getBlueprints(request.pattern, request.customStepTitles);
    if (blueprints.empty()) {
        return {false, "No steps generated for this decomposition pattern.", {}, {}, std::nullopt};
    }

    std::vector<Task> subtasks;
    subtasks.reserve(blueprints.size());

    const auto now = std::chrono::system_clock::now();
    int allocatedMinutes = 0;

    for (std::size_t i = 0; i < blueprints.size(); ++i) {
        const auto& bp = blueprints[i];
        Task task;
        task.id = request.parentTaskId + "_" + bp.suffix;
        task.title = bp.title;
        task.courseId = request.courseId;
        task.priority = request.priority;
        task.status = TaskStatus::Todo;

        // Phân bổ thời lượng phút
        if (i + 1 == blueprints.size()) {
            // Bước cuối cùng nhận số phút còn lại để tổng khớp chính xác
            task.estimatedMinutes = std::max(0, request.totalEstimatedMinutes - allocatedMinutes);
        } else {
            task.estimatedMinutes = static_cast<int>(std::round(request.totalEstimatedMinutes * bp.durationWeight));
            allocatedMinutes += task.estimatedMinutes;
        }
        task.remainingMinutes = task.estimatedMinutes;

        // Tính deadline trung gian theo tỷ lệ tích lũy nếu có deadline cha
        if (request.deadline.has_value()) {
            if (*request.deadline > now) {
                const auto totalDuration = *request.deadline - now;
                const auto offset = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    totalDuration * bp.cumulativeRatio);
                task.deadline = now + offset;
            } else {
                task.deadline = *request.deadline;
            }
        }

        // Thiết lập quan hệ phụ thuộc: bước sau phụ thuộc trực tiếp bước trước
        if (i > 0) {
            task.dependencies.push_back(subtasks[i - 1].id);
        }

        subtasks.push_back(std::move(task));
    }

    // Xây dựng và kiểm tra đồ thị DAG phụ thuộc
    DependencyDAG dag;
    try {
        dag = DependencyDAG::fromTasks(subtasks);
    } catch (const std::exception& e) {
        return {false, std::string("DAG generation failed: ") + e.what(), {}, {}, std::nullopt};
    }

    // Lưu vào Repository nếu được cung cấp
    if (repository_) {
        for (const auto& task : subtasks) {
            if (!repository_->add(task)) {
                // Tiếp tục thử thêm, không làm gãy toàn bộ nếu đã trùng
            }
        }
    }

    // Khởi tạo Auto-Study Workspace nếu có yêu cầu
    std::optional<BootstrapResult> workspaceRes;
    if (request.bootstrapWorkspace) {
        if (!bootstrapper_) {
            if (environment_) {
                bootstrapper_ = std::make_unique<WorkspaceBootstrapper>(environment_);
            }
        }
        if (bootstrapper_) {
            WorkspaceConfig wsConfig;
            wsConfig.courseId = request.courseId;
            wsConfig.taskId = request.parentTaskId;
            wsConfig.title = request.title;
            wsConfig.subtasks = subtasks;
            wsConfig.referenceLinks = request.referenceLinks;
            workspaceRes = bootstrapper_->bootstrap(wsConfig);
        } else {
            BootstrapResult failedBoot;
            failedBoot.success = false;
            failedBoot.message = "No environment provided to bootstrap workspace.";
            workspaceRes = failedBoot;
        }
    }

    return {
        true,
        "Successfully decomposed task into " + std::to_string(subtasks.size()) + " subtasks.",
        std::move(subtasks),
        std::move(dag),
        workspaceRes
    };
}

// ============================================================================
// TaskDecomposerTool
// ============================================================================

TaskDecomposerTool::TaskDecomposerTool(
    std::shared_ptr<ITaskRepository> repository,
    std::shared_ptr<const IEnvironment> environment)
    : decomposer_(std::move(repository), std::move(environment)) {}

std::string_view TaskDecomposerTool::name() const noexcept {
    return "task_decomposer";
}

std::string_view TaskDecomposerTool::description() const noexcept {
    return "Decomposes a major educational task into ordered subtasks with DAG dependencies and bootstraps workspace.";
}

ToolResult TaskDecomposerTool::execute(std::string_view input) {
    try {
        const auto req = json::parse(input);
        if (!req.is_object()) {
            return {false, "Expected JSON object input for task_decomposer."};
        }

        std::string parentId;
        if (req.contains("task_id") && req["task_id"].is_string()) {
            parentId = req["task_id"].get<std::string>();
        } else if (req.contains("parent_task_id") && req["parent_task_id"].is_string()) {
            parentId = req["parent_task_id"].get<std::string>();
        } else {
            return {false, "Missing required field: task_id or parent_task_id."};
        }

        DecompositionRequest decReq;
        decReq.parentTaskId = parentId;
        decReq.title = req.value("title", parentId);
        decReq.courseId = req.value("course_id", "");
        decReq.totalEstimatedMinutes = req.value("estimated_minutes", 120);
        decReq.bootstrapWorkspace = req.value("bootstrap_workspace", false);

        if (req.contains("pattern") && req["pattern"].is_string()) {
            decReq.pattern = parsePattern(req["pattern"].get<std::string>());
        }

        if (req.contains("custom_steps") && req["custom_steps"].is_array()) {
            decReq.pattern = DecompositionPattern::Custom;
            for (const auto& item : req["custom_steps"]) {
                if (item.is_string()) {
                    decReq.customStepTitles.push_back(item.get<std::string>());
                }
            }
        }

        if (req.contains("reference_links") && req["reference_links"].is_array()) {
            for (const auto& link : req["reference_links"]) {
                if (link.is_string()) {
                    decReq.referenceLinks.push_back(link.get<std::string>());
                }
            }
        }

        const auto res = decomposer_.decompose(decReq);
        if (!res.success) {
            return {false, res.message};
        }

        json output;
        output["parent_task_id"] = parentId;
        output["pattern"] = patternToString(decReq.pattern);
        output["subtasks_count"] = res.subtasks.size();

        json subtaskList = json::array();
        for (const auto& st : res.subtasks) {
            json item;
            item["id"] = st.id;
            item["title"] = st.title;
            item["course_id"] = st.courseId;
            item["estimated_minutes"] = st.estimatedMinutes;
            item["dependencies"] = st.dependencies;
            subtaskList.push_back(item);
        }
        output["subtasks"] = subtaskList;

        if (res.workspace.has_value()) {
            output["workspace_bootstrapped"] = res.workspace->success;
            output["workspace_message"] = res.workspace->message;
            if (res.workspace->success) {
                output["workspace_path"] = res.workspace->rootPath.string();
            }
        } else {
            output["workspace_bootstrapped"] = false;
        }

        return {true, output.dump(2)};
    } catch (const json::exception& e) {
        return {false, std::string("JSON parsing error in task_decomposer: ") + e.what()};
    } catch (const std::exception& e) {
        return {false, std::string("Decomposition failed: ") + e.what()};
    }
}

} // namespace edu_schedule
