// Kiểm tra client; không sao chép chương trình main.
#include "client/ollama_client.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace edu_schedule;

void check(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

int main(int argc, char* argv[]) {
    auto file = std::filesystem::temp_directory_path() /
        ("ollama_base64_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    try {
        CurlGlobalGuard guard;
        OllamaClient client;
        check(!client.chat(std::vector<ChatMessage>{}).success, "Empty messages must fail");
        // Các byte mẫu kiểm tra phép mã hóa, không phải ảnh để model nhận diện.
        const std::vector<std::string> bytes = {"f", "fo", "foo"};
        const std::vector<std::string> expected = {"Zg==", "Zm8=", "Zm9v"};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            { std::ofstream output(file, std::ios::binary); output << bytes[i]; }
            check(LLMClient::load_image_as_base64(file) == expected[i], "Base64 mismatch");
        }
        std::filesystem::remove(file);
        check(!LLMClient::load_image_as_base64(file), "Missing image must fail");

        // Server giả lập được khởi động bởi ollama_http_test.py.
        if (argc == 2) {
            LLMConfig config;
            config.base_url = argv[1];
            config.model_name = "valid";
            client.set_config(config);
            auto response = client.chat("hello");
            if (!response.success) std::cerr << response.error_message.value_or("No error detail") << '\n';
            check(response.success && response.total_tokens == 5, "Valid response failed");
            for (const std::string model : {"bad-json", "bad-schema", "bad-metadata"}) {
                config.model_name = model;
                client.set_config(config);
                response = client.chat("hello");
                check(!response.success && response.error_type == LLMErrorType::MalformedJson,
                      "Invalid JSON/schema was accepted");
            }
            config.model_name = "http-error";
            client.set_config(config);
            response = client.chat("hello");
            check(!response.success && response.http_status == 500, "HTTP error not reported");
            config.model_name = "slow";
            config.timeout_ms = 30;
            client.set_config(config);
            response = client.chat("hello");
            check(!response.success && response.error_type == LLMErrorType::Timeout, "Timeout not reported");
        }
        std::cout << "Ollama client checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
