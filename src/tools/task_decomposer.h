#pragma once

#include "../agent/Tool.h"
#include "../harness/environment.h"
#include "../models/Task.h"
#include "../models/TaskRepository.h"
#include "dependency_dag.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edu_schedule {

// Mẫu phân rã công việc chuẩn hóa theo loại hình học tập / dự án
enum class DecompositionPattern {
    SoftwareProject, // Phân tích yêu cầu -> UML Design -> Cài đặt -> Unit test -> Báo cáo
    LabAssignment,   // Chuẩn bị lý thuyết -> Cài đặt bài lab -> Kiểm tra đánh giá -> Báo cáo lab
    ResearchEssay,   // Đọc nghiên cứu -> Lập dàn ý -> Viết bản thảo -> Rà soát trích dẫn
    Custom           // Phân rã theo danh sách các bước tùy biến do người dùng định nghĩa
};

// Cấu hình không gian làm việc học tập (Workspace Scaffold Configuration)
struct WorkspaceConfig {
    std::string courseId;
    std::string taskId;
    std::string title;
    std::vector<Task> subtasks;
    std::vector<std::string> referenceLinks;
};

// Kết quả sau khi khởi tạo Auto-Study Workspace
struct BootstrapResult {
    bool success = false;
    std::string message;
    std::filesystem::path rootPath;
    std::vector<std::filesystem::path> createdFiles;
};

// ============================================================================
// WorkspaceBootstrapper: Tự động khởi tạo thư mục và tệp khung học tập
//
// Đảm bảo an toàn:
// - Giới hạn tuyệt đối bên trong thư mục môi trường được cấp phép.
// - Chặn Path Traversal thông qua kiểm tra IEnvironment::isPathAllowed.
// - Tự động tạo cấu trúc chuẩn: docs/, src/, tests/, reports/ và tệp mẫu.
// ============================================================================
class WorkspaceBootstrapper {
public:
    explicit WorkspaceBootstrapper(std::shared_ptr<const IEnvironment> environment);

    [[nodiscard]] BootstrapResult bootstrap(const WorkspaceConfig& config) const;

private:
    std::shared_ptr<const IEnvironment> environment_;

    [[nodiscard]] bool writeTemplateFile(const std::filesystem::path& targetPath,
                                         std::string_view content) const;
    [[nodiscard]] std::string generateReadme(const WorkspaceConfig& config) const;
    [[nodiscard]] std::string generateRequirements(const WorkspaceConfig& config) const;
    [[nodiscard]] std::string generateReferences(const WorkspaceConfig& config) const;
    [[nodiscard]] std::string generateStarterCode(const WorkspaceConfig& config) const;
    [[nodiscard]] std::string generateReportTemplate(const WorkspaceConfig& config) const;
};

// Thông tin yêu cầu phân rã nhiệm vụ
struct DecompositionRequest {
    std::string parentTaskId;
    std::string title;
    std::string courseId;
    int totalEstimatedMinutes = 0;
    std::optional<TimePoint> deadline;
    Priority priority = Priority::Mid;
    DecompositionPattern pattern = DecompositionPattern::SoftwareProject;
    std::vector<std::string> customStepTitles; // Dành cho pattern Custom
    std::vector<std::string> referenceLinks;
    bool bootstrapWorkspace = false;
};

// Kết quả thực thi phân rã
struct DecompositionResult {
    bool success = false;
    std::string message;
    std::vector<Task> subtasks;
    DependencyDAG dag;
    std::optional<BootstrapResult> workspace;
};

// ============================================================================
// TaskDecomposer: Chia nhỏ nhiệm vụ lớn thành chuỗi các nhiệm vụ con có thứ tự
//
// Nghiệp vụ cốt lõi:
// - Phân bổ thời lượng ước tính hợp lý theo tỷ lệ giai đoạn.
// - Tự động tính toán mốc hoàn thành trung gian dựa trên deadline cha.
// - Xây dựng đồ thị phụ thuộc DAG chuẩn xác (không trùng lặp, không chu trình).
// - Lưu trữ vào TaskRepository và gọi WorkspaceBootstrapper khi được kích hoạt.
// ============================================================================
class TaskDecomposer {
public:
    explicit TaskDecomposer(
        std::shared_ptr<ITaskRepository> repository = nullptr,
        std::shared_ptr<const IEnvironment> environment = nullptr);

    [[nodiscard]] DecompositionResult decompose(const DecompositionRequest& request);

    [[nodiscard]] std::shared_ptr<ITaskRepository> getRepository() const noexcept {
        return repository_;
    }

    [[nodiscard]] std::shared_ptr<const IEnvironment> getEnvironment() const noexcept {
        return environment_;
    }

private:
    std::shared_ptr<ITaskRepository> repository_;
    std::shared_ptr<const IEnvironment> environment_;
    std::unique_ptr<WorkspaceBootstrapper> bootstrapper_;

    struct StepBlueprint {
        std::string suffix;
        std::string title;
        double durationWeight = 0.0; // Tỷ trọng thời lượng (tổng = 1.0)
        double cumulativeRatio = 0.0; // Tỷ lệ hoàn thành để tính deadline trung gian
    };

    [[nodiscard]] std::vector<StepBlueprint> getBlueprints(
        DecompositionPattern pattern,
        const std::vector<std::string>& customTitles) const;
};

// ============================================================================
// TaskDecomposerTool: Đóng gói TaskDecomposer thành Tool cho AgentLoop
// ============================================================================
class TaskDecomposerTool final : public ITool {
public:
    explicit TaskDecomposerTool(
        std::shared_ptr<ITaskRepository> repository = nullptr,
        std::shared_ptr<const IEnvironment> environment = nullptr);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] ToolResult execute(std::string_view input) override;

private:
    TaskDecomposer decomposer_;
};

} // namespace edu_schedule
