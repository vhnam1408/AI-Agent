#include "agent/deadline_extractor.h"
#include "client/ollama_client.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace edu_schedule;
namespace {
// Đọc văn bản nguồn UTF-8 nguyên byte; đường dẫn và id được giữ trong kết quả.
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read source: " + path);
    }
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace

// CLI chạy trên Kaggle: --text FILE URL MODEL [YEAR] hoặc --image FILE URL MODEL.
// Không tạo Task hoặc ghi repository; stdout chỉ chứa extraction result/error JSON.
int main(int argc, char** argv) {
    try {
        if (argc < 5) {
            throw std::runtime_error("--text FILE URL MODEL [YEAR] | --image FILE URL MODEL");
        }
        const std::string mode = argv[1];
        ExtractionRequest request;
        request.source.id = argv[2];
        if (mode == "--text") {
            request.source.text = read_file(argv[2]);
        } else if (mode == "--image") {
            auto image = LLMClient::load_image_as_base64(argv[2]);
            if (!image) {
                throw std::runtime_error("Cannot load source image");
            }
            request.source.base64_images.push_back(std::move(*image));
        } else {
            throw std::runtime_error("Unknown mode");
        }
        if (argc >= 6) {
            if (mode != "--text") {
                throw std::runtime_error("Reference year is text-context only in this probe");
            }
            const std::string year = argv[5];
            if (year.size() != 4 || year.find_first_not_of("0123456789") != std::string::npos) {
                throw std::runtime_error("Year must have four digits");
            }
            request.reference_year = std::stoi(year);
        }
        CurlGlobalGuard guard;
        LLMConfig config;
        config.base_url = argv[3];
        config.model_name = argv[4];
        config.temperature = 0;
        config.max_tokens = 8192;
        config.timeout_ms = 900000;
        OllamaClient client(config);
        DeadlineExtractor extractor(client);
        auto result = extractor.extract(request);
        if (!result) {
            std::cout << deadline_extraction_error_to_json(result.error()).dump(2) << '\n';
            return 1;
        }
        std::cout << deadline_extraction_to_json(*result).dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cout << nlohmann::json{{"ok", false}, {"error", "ProbeFailure"}, {"message", e.what()}}
                         .dump(2)
                  << '\n';
        return 1;
    }
}
