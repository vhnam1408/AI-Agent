#include "harness/trajectory.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using namespace edu_schedule;

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::print(stderr, "[FAILED] {}\n", message);
        std::exit(EXIT_FAILURE);
    }
}

// Hàm tiện ích tạo mốc thời gian UTC phục vụ kiểm thử độc lập
TimePoint makeUtcTime(int year, unsigned month, unsigned day, int hour, int minute, int second = 0) {
    const auto ymd = std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day};
    const auto dayPoint = std::chrono::sys_days{ymd};
    return dayPoint + std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
}

} // namespace

int main() {
    std::print("=== BAT DAU KIEM THU TASK-16: TRAJECTORY JSON & RFC 5545 ICS EXPORTER ===\n");

    // 1. Kiểm thử Step & Trajectory xuất JSON và lưu file (trajectory.cpp)
    Trajectory traj("task_16_demo", "qwen3.5:9b");

    Step step1;
    step1.step_id = 1;
    step1.thought = "Doc danh sach cong viec va xep 3 khung gio hoc";
    step1.action_type = "tool_call";
    step1.tool_name = "schedule_solver";
    step1.tool_args = R"({"course":"OOP"})";
    step1.tool_result = "Scheduled 3 study sessions";
    step1.tool_success = true;
    step1.tokens_used = 145;
    step1.latency_ms = 230;
    traj.addStep(step1);

    Step step2;
    step2.step_id = 2;
    step2.thought = "Xuat lich hoc ra file sample.ics chuan RFC 5545";
    step2.action_type = "final_answer";
    step2.tokens_used = 65;
    step2.latency_ms = 80;
    traj.addStep(step2);

    traj.setSuccess(true);
    traj.setTotalTimeMs(310);
    traj.setFinalAnswer("Da xuat lich hoc thanh cong ra file sample.ics.");

    const auto jsonOut = traj.toJson();
    check(jsonOut["task_id"] == "task_16_demo", "Sai task_id trong Trajectory JSON");
    check(jsonOut["model"] == "qwen3.5:9b", "Sai model trong Trajectory JSON");
    check(jsonOut["success"] == true, "Sai trang thai success trong Trajectory JSON");
    check(jsonOut["total_tokens"] == 210, "Sai total_tokens trong Trajectory JSON");
    check(jsonOut["total_time_ms"] == 310, "Sai total_time_ms trong Trajectory JSON");
    check(jsonOut["steps"].size() == 2, "Sai so luong steps trong Trajectory JSON");
    check(jsonOut["steps"][0]["action"]["tool"] == "schedule_solver", "Sai ten tool trong Step 1");

    const std::filesystem::path tempJsonPath = "test_trajectory_task16.json";
    const bool jsonSaved = traj.saveToFile(tempJsonPath);
    check(jsonSaved, "Khong the luu file JSON tam");
    check(std::filesystem::exists(tempJsonPath), "File JSON tam khong ton tai");
    std::filesystem::remove(tempJsonPath);
    std::print("[PASS] trajectory.cpp xuat JSON va luu file chinh xac.\n");

    // 2. Tạo danh sách 3 khung giờ học giả lập (Mock TimeSlot) theo đúng yêu cầu TASK-16
    // Giờ Việt Nam (UTC+7):
    // - Slot 1: 02/11/2026 08:00 - 10:00 ICT -> 01:00 - 03:00 UTC
    // - Slot 2: 02/11/2026 14:00 - 16:00 ICT -> 07:00 - 09:00 UTC
    // - Slot 3: 03/11/2026 19:00 - 21:00 ICT -> 12:00 - 14:00 UTC
    const TimeSlot slot1{
        makeUtcTime(2026, 11, 2, 1, 0),
        makeUtcTime(2026, 11, 2, 3, 0)
    };
    const TimeSlot slot2{
        makeUtcTime(2026, 11, 2, 7, 0),
        makeUtcTime(2026, 11, 2, 9, 0)
    };
    const TimeSlot slot3{
        makeUtcTime(2026, 11, 3, 12, 0),
        makeUtcTime(2026, 11, 3, 14, 0)
    };

    check(slot1.isValid() && slot2.isValid() && slot3.isValid(), "TimeSlot khong hop le");
    check(!slot1.overlaps(slot2), "Slot 1 va Slot 2 bi chong lan");

    const std::vector<IcsEvent> mockEvents = {
        {
            "session-oop-20261102@eduschedule.agent",
            "Hoc OOP - Hien thuc Trajectory & ICS",
            "On tap C++20 std::format, C++23 std::print; viet unit test.",
            slot1
        },
        {
            "session-dsa-20261102@eduschedule.agent",
            "Hoc CTDL & GT - Thuat toan Topological Sort",
            "Giai bai tap do thi DAG\nKiem tra chu trinh.",
            slot2
        },
        {
            "session-db-20261103@eduschedule.agent",
            "Hoc Co so du lieu - Thuc hanh SQLite",
            "Thiet ke bang courses, tasks, memories.",
            slot3
        }
    };

    const TimePoint fixedStamp = makeUtcTime(2026, 10, 30, 15, 0, 0);
    check(
        IcsExporter::formatUtcTimestamp(fixedStamp) == "20261030T150000Z",
        "Dinh dang UTC timestamp sai");

    const std::filesystem::path sampleIcsPath = "sample.ics";
    const bool icsSaved = IcsExporter::saveToFile(sampleIcsPath, mockEvents, fixedStamp);
    check(icsSaved, "IcsExporter::saveToFile that bai");
    check(std::filesystem::exists(sampleIcsPath), "File sample.ics khong ton tai tren o dia");

    // Đọc file ở chế độ binary để kiểm chứng chính xác CRLF (\r\n) và cấu trúc RFC 5545
    std::ifstream inFile(sampleIcsPath, std::ios::binary);
    check(inFile.is_open(), "Khong the mo file sample.ics de kiem tra");
    const std::string content(
        (std::istreambuf_iterator<char>(inFile)),
        std::istreambuf_iterator<char>());
    inFile.close();

    check(content.find("BEGIN:VCALENDAR\r\n") == 0, "Thieu BEGIN:VCALENDAR");
    check(content.find("VERSION:2.0\r\n") != std::string::npos, "Thieu VERSION:2.0");
    check(
        content.find("UID:session-oop-20261102@eduschedule.agent\r\n") != std::string::npos,
        "Thieu UID su kien 1");
    check(content.find("DTSTAMP:20261030T150000Z\r\n") != std::string::npos, "Thieu DTSTAMP");
    check(content.find("DTSTART:20261102T010000Z\r\n") != std::string::npos, "Thieu DTSTART slot 1");
    check(content.find("DTEND:20261102T030000Z\r\n") != std::string::npos, "Thieu DTEND slot 1");
    check(content.find("DTSTART:20261102T070000Z\r\n") != std::string::npos, "Thieu DTSTART slot 2");
    check(content.find("DTSTART:20261103T120000Z\r\n") != std::string::npos, "Thieu DTSTART slot 3");
    // Kiểm chứng escape ký tự ',' ';' và '\n' theo RFC 5545
    check(
        content.find("On tap C++20 std::format\\, C++23 std::print\\; viet unit test.\r\n") != std::string::npos,
        "Escape dau phay va cham phay sai");
    check(
        content.find("Giai bai tap do thi DAG\\nKiem tra chu trinh.\r\n") != std::string::npos,
        "Escape ky tu xuong dong sai");
    check(content.find("\r\r\n") == std::string::npos, "Loi lap ky tu CR (\\r\\r\\n)");
    check(content.ends_with("END:VCALENDAR\r\n"), "Thieu END:VCALENDAR");

    std::print(
        "[PASS] Xuat file {} chuan RFC 5545 bang std::format & std::print thanh cong!\n",
        std::filesystem::absolute(sampleIcsPath).string());
    std::print("=== TAT CA TEST TASK-16 DA VUOT QUA [100% PASS] ===\n");
    return 0;
}
