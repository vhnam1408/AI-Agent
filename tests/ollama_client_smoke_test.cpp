// Kiểm tra Client và std::expected, không cần cài model Ollama.
#include "client/ollama_client.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace edu_schedule;

void check(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

void check_error(const LLMResult& result, LLMErrorType type) {
    check(!result, "Expected an error, received a value");
    if (result.error().type != type) {
        throw std::runtime_error(std::string("Expected ") + std::string(to_string(type)) +
            ", received " + std::string(to_string(result.error().type)) + ": " + result.error().message);
    }
    check(!result.error().message.empty(), "Error message must not be empty");
}

int main(int argc, char* argv[]) {
    static_assert(std::is_same_v<LLMResult, std::expected<LLMResponse, LLMError>>);
    auto file = std::filesystem::temp_directory_path() /
        ("ollama_base64_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    try {
        CurlGlobalGuard guard;
        OllamaClient client;
        check_error(client.chat(std::vector<ChatMessage>{}), LLMErrorType::InvalidRequest);

        // Cấu hình sai phải bị từ chối trước khi gửi HTTP.
        for (int test = 0; test < 6; ++test) {
            LLMConfig config;
            if (test == 0) config.model_name = " ";
            if (test == 1) config.base_url = "file:///not-http";
            if (test == 2) config.temperature = -1;
            if (test == 3) config.temperature = std::numeric_limits<float>::quiet_NaN();
            if (test == 4) config.timeout_ms = 0;
            if (test == 5) config.connect_timeout_ms = -1;
            client.set_config(config);
            check_error(client.chat("hello"), LLMErrorType::InvalidRequest);
        }

        // Các byte mẫu kiểm tra Base64, không phải ảnh thật để model nhận diện.
        const std::vector<std::string> bytes = {"f", "fo", "foo"};
        const std::vector<std::string> expected = {"Zg==", "Zm8=", "Zm9v"};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            { std::ofstream output(file, std::ios::binary); output << bytes[i]; }
            check(LLMClient::load_image_as_base64(file) == expected[i], "Base64 mismatch");
        }
        std::filesystem::remove(file);
        check(!LLMClient::load_image_as_base64(file), "Missing image must fail");

        // Server và cổng từ chối kết nối do ollama_http_test.py cung cấp.
        if (argc == 3) {
            LLMConfig config;
            config.base_url = argv[1];
            config.model_name = "valid";
            client.set_config(config);
            auto response = client.chat("hello");
            if (!response) std::cerr << response.error().message << '\n';
            check(response && response->total_tokens == 5 && response->http_status == 200,
                  "Valid response failed");
            for (const std::string model : {"bad-json", "bad-schema", "bad-metadata",
                    "missing-field", "negative-tokens", "large-tokens", "float-tokens",
                    "overflow-tokens", "bad-duration", "array-json", "null-json"}) {
                client.switch_model(model);
                check_error(client.chat("hello"), LLMErrorType::MalformedJson);
            }
            for (const auto& model : {"http-error", "not-found"}) {
                client.switch_model(model);
                response = client.chat("hello");
                check_error(response, LLMErrorType::HttpError);
                check(response.error().http_status == (std::string(model) == "http-error" ? 500 : 404),
                      "HTTP status was lost");
            }
            client.switch_model("disconnect");
            check_error(client.chat("hello"), LLMErrorType::NetworkError);

            config.model_name = "slow";
            config.timeout_ms = 30;
            client.set_config(config);
            check_error(client.chat("hello"), LLMErrorType::Timeout);

            config.base_url = argv[2];
            // Windows có thể mất hơn 1 giây để báo cổng đóng; tránh nhầm với timeout.
            config.timeout_ms = 5000;
            config.connect_timeout_ms = 5000;
            client.set_config(config);
            check_error(client.chat("hello"), LLMErrorType::ConnectionRefused);

            // Client phải dùng được sau lỗi và không nối lặp /api/chat.
            config.base_url = std::string(argv[1]) + "/api/chat/";
            config.model_name = "reconfigured";
            config.temperature = 0.5f;
            config.max_tokens = 64;
            client.set_config(config);
            response = client.chat("after errors");
            check(response && response->model_used == "reconfigured", "Client did not recover");
        }
        std::cout << "Ollama expected results, config, HTTP, network, timeout and JSON checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
