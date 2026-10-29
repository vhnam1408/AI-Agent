#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace edu_schedule {

// ============================================================================
// IEnvironment: Interface thuần ảo đại diện cho môi trường thực thi (Phòng thi)
// Đảm bảo tính độc lập: Agent không quan tâm mình đang chạy trên máy thật hay hộp cát
// ============================================================================
class IEnvironment {
public:
    virtual ~IEnvironment() = default;

    // Chuẩn bị môi trường trước khi Agent bắt đầu làm bài
    virtual void setup() = 0;

    // Dọn dẹp tài nguyên sau khi Agent hoàn thành bài làm
    virtual void teardown() = 0;

    // Lấy đường dẫn thư mục làm việc hiện tại của môi trường
    [[nodiscard]] virtual std::filesystem::path getWorkingDir() const = 0;

    // Kiểm tra xem môi trường hiện tại có phải là Sandbox bảo mật hay không
    [[nodiscard]] virtual bool isSandbox() const noexcept = 0;

    // Kiểm tra xem một đường dẫn file có được phép truy cập trong môi trường không
    [[nodiscard]] virtual bool isPathAllowed(const std::filesystem::path& targetPath) const = 0;
};

// ============================================================================
// NativeEnvironment: Môi trường chạy trực tiếp trên máy chủ / thư mục dự án
// ============================================================================
class NativeEnvironment : public IEnvironment {
public:
    explicit NativeEnvironment(std::filesystem::path workingDir = std::filesystem::current_path())
        : workingDir_(std::move(workingDir)) {}

    void setup() override {
        // Native không cần tạo môi trường cách ly đặc biệt
    }

    void teardown() override {
        // Native giữ nguyên trạng thái làm việc
    }

    [[nodiscard]] std::filesystem::path getWorkingDir() const override {
        return workingDir_;
    }

    [[nodiscard]] bool isSandbox() const noexcept override {
        return false;
    }

    [[nodiscard]] bool isPathAllowed(const std::filesystem::path& targetPath) const override {
        (void)targetPath;
        return true; // Native cho phép truy cập theo quyền người dùng
    }

private:
    std::filesystem::path workingDir_;
};

// ============================================================================
// SandboxEnvironment: Môi trường hộp cát cách ly, giới hạn phạm vi truy cập file
// ============================================================================
class SandboxEnvironment : public IEnvironment {
public:
    explicit SandboxEnvironment(std::filesystem::path sandboxDir)
        : sandboxDir_(std::move(sandboxDir)) {}

    void setup() override {
        std::error_code ec;
        if (!std::filesystem::exists(sandboxDir_, ec)) {
            std::filesystem::create_directories(sandboxDir_, ec);
        }
    }

    void teardown() override {
        // Có thể dọn dẹp thư mục tạm sau khi kết thúc đánh giá
    }

    [[nodiscard]] std::filesystem::path getWorkingDir() const override {
        return sandboxDir_;
    }

    [[nodiscard]] bool isSandbox() const noexcept override {
        return true;
    }

    // Chặn tấn công path traversal (../) ra ngoài phạm vi thư mục sandbox
    [[nodiscard]] bool isPathAllowed(const std::filesystem::path& targetPath) const override {
        try {
            const auto canonicalSandbox = std::filesystem::weakly_canonical(sandboxDir_);
            const auto canonicalTarget = std::filesystem::weakly_canonical(targetPath);
            const auto sboxStr = canonicalSandbox.lexically_normal().string();
            const auto targetStr = canonicalTarget.lexically_normal().string();
            return targetStr.starts_with(sboxStr);
        } catch (...) {
            return false;
        }
    }

private:
    std::filesystem::path sandboxDir_;
};

} // namespace edu_schedule

