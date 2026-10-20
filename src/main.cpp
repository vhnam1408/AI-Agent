// Thử API: đọc đầu vào -> gọi Ollama -> in câu trả lời -> lưu kết quả JSON.
#include "client/ollama_client.h"
#include "agent/loop_detector.h"
#include "agent/agent_loop.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace edu_schedule;
using json = nlohmann::json;

namespace console {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* MAGENTA = "\033[35m";
}

// Đọc nguyên nội dung file văn bản. File ảnh được đọc riêng trong LLMClient.
std::string read_text(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Khong mo duoc file: " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3); // Bỏ BOM UTF-8.
    if (text.empty()) throw std::runtime_error("File van ban rong: " + path);
    return text;
}

// JSON của API đã được Client kiểm tra. Nội dung AI có thể là văn bản thường.
void save_result(const LLMResponse& response, const LLMConfig& config,
                 const std::string& input_type, const std::string& path) {
    json data = {
        {"model_requested", config.model_name}, {"model_used", response.model_used},
        {"input_type", input_type}, {"success", response.success},
        {"content", response.content}, {"latency_ms", response.latency_ms},
        {"truncated", response.truncated}
    };
    std::filesystem::path output(path);
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    std::ofstream file(output);
    if (!file) throw std::runtime_error("Khong tao duoc file: " + path);
    file << data.dump(2) << '\n';
    file.close();
    if (!file) throw std::runtime_error("Ghi file that bai: " + path);
    std::cout << data.dump(2) << "\nDa luu: " << path << '\n';
}

// Đọc số và từ chối cả trường hợp nhập nhầm như "100abc".
double read_number(const std::string& text) {
    std::size_t end = 0;
    double value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value)) {
        throw std::runtime_error("Gia tri so khong hop le: " + text);
    }
    return value;
}

void print_usage() {
    std::cout << "Cach chay:\n"
              << "  ai_agent --model TEN_MODEL --text assets/chat_prompt.txt\n"
              << "  ai_agent --model TEN_MODEL_VISION --image assets/timetable.png\n"
              << "Tuy chon: --url URL --output FILE --temperature 0.2\n"
              << "          --max-tokens 256 --timeout-ms 120000 --insecure\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
    // ── TASK-05: Milestone G1 Architecture Freeze Verification ────────────────
    std::cout << console::MAGENTA << "\n[TASK-05 G1 Freeze] Khởi tạo LoopDetector & AgentLoop interfaces..." << console::RESET << "\n";
    LoopDetector loop_detector(LoopDetector::Config{ .warning_threshold = 2, .critical_threshold = 4, .history_capacity = 30 });
    loop_detector.recordAction("task_lookup:T1");
    auto loop_test = loop_detector.recordAndCheck("task_lookup:T1");
    std::cout << console::GREEN << "  ✓ LoopDetector: Generic Repeat (ngưỡng 2) → "
              << (loop_test.isWarning() ? "Cảnh báo chính xác [PASS]" : "Chưa cảnh báo") << "\n";
    loop_detector.reset();

    AgentLoop agent_loop(std::shared_ptr<LLMClient>{}, /*max_steps=*/5, loop_detector);
    agent_loop.setStepHook([](const StepEvent& ev) {
        (void)ev;
    });
    std::cout << "  ✓ AgentLoop: Template Method (run) & Observer Hook (StepHook) sẵn sàng [PASS]\n"
              << console::RESET;

        // 1. Đọc cấu hình. Tên model phải đúng với model đã cài trong Ollama.
        LLMConfig config;
        config.model_name.clear();
        std::string text_path, image_path;
        std::string output_path = "output/result.json";
        if (argc == 1) { print_usage(); return 0; }
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--help") { print_usage(); return 0; }
            if (option == "--insecure" || option == "--no-verify-ssl") {
                config.verify_ssl = false;
                continue;
            }
            if (i + 1 >= argc) throw std::runtime_error("Thieu gia tri cho " + option);
            std::string value = argv[++i];
            if (option == "--model") config.model_name = value;
            else if (option == "--url") config.base_url = value;
            else if (option == "--text") text_path = value;
            else if (option == "--image") image_path = value;
            else if (option == "--output") output_path = value;
            else if (option == "--temperature") {
                double number = read_number(value);
                if (number < 0 || number > 2) throw std::runtime_error("temperature: tu 0 den 2.");
                config.temperature = static_cast<float>(number);
            } else if (option == "--max-tokens" || option == "--timeout-ms") {
                double number = read_number(value);
                if (number < 1 || number > 2147483647 || std::floor(number) != number) {
                    throw std::runtime_error(option + ": can so nguyen duong, toi da 2147483647.");
                }
                if (option == "--max-tokens") config.max_tokens = static_cast<int>(number);
                else config.timeout_ms = static_cast<long>(number);
            }
            else throw std::runtime_error("Tham so khong hop le: " + option);
        }
        if (config.model_name.empty()) throw std::runtime_error("Can truyen --model TEN_MODEL.");
        bool has_text = !text_path.empty();
        bool has_image = !image_path.empty();
        if ((has_text && has_image) || (!has_text && !has_image)) {
            throw std::runtime_error("Chon dung mot dau vao: --text hoac --image.");
        }

        // 2. Tạo yêu cầu. Ảnh được đổi thành chuỗi Base64 để đặt vào JSON.
        ChatMessage message;
        if (!text_path.empty()) {
            message = ChatMessage::user(read_text(text_path));
        } else {
            auto image = LLMClient::load_image_as_base64(image_path);
            if (!image) throw std::runtime_error("Khong doc duoc anh: " + image_path);
            message = ChatMessage::user_with_images(
                "Describe what you can see in this image. Do not guess unreadable text.", {*image});
        }

        // 3. Gửi HTTP qua OllamaClient. Guard tự dọn tài nguyên curl khi ra khỏi khối.
        CurlGlobalGuard curl_guard;
        OllamaClient client(config);
        std::cout << "Dang goi model: " << config.model_name << '\n';
        // Text và ảnh đều dùng chung hàm chat(), đúng giao diện trong đề bài.
        LLMResponse response = client.chat({message});
        if (!response.success) {
            std::cerr << "Loi " << to_string(response.error_type.value_or(LLMErrorType::HttpError))
                      << ": " << response.error_message.value_or("Khong ro nguyen nhan") << '\n';
            return 1;
        }

        // 4. Lưu bản ghi thử API. Chưa trích xuất hay xác minh deadline ở bước này.
        if (response.truncated) std::cerr << "Luu y: phan hoi bi cat; co the tang --max-tokens.\n";
        save_result(response, config, text_path.empty() ? "image" : "text", output_path);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Loi: " << error.what() << '\n';
        return 1;
    }
}
