cmake_minimum_required(VERSION 3.20)
project(task017_deadline_extractor LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>
int main() { std::expected<int, int> value = 1; return value.value() - 1; }"
    TASK21_HAS_STD_EXPECTED)
if(NOT TASK21_HAS_STD_EXPECTED)
    message(FATAL_ERROR "TASK-021 requires C++23 std::expected; select a supported Kaggle toolchain.")
endif()
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include(CTest)
find_package(CURL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Module chỉ đọc domain_types.h và dùng interface LLMClient; không link model/repository.
add_library(task17_extractor STATIC "${REPO}/src/agent/deadline_extractor.cpp")
target_include_directories(task17_extractor PUBLIC "${REPO}/include" "${REPO}/src")
if(MSVC)
    target_compile_options(task17_extractor PRIVATE /W4 /utf-8)
else()
    target_compile_options(task17_extractor PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(deadline_extractor_test deadline_extractor_test.cpp)
target_link_libraries(deadline_extractor_test PRIVATE task17_extractor)
add_test(NAME task17_unit COMMAND deadline_extractor_test "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_BINARY_DIR}/unit_results.json")

# Probe live dùng lại HTTP/Base64 của Client, không sửa code Client TASK-013.
add_executable(deadline_extractor_probe deadline_extractor_probe.cpp "${REPO}/src/client/ollama_client.cpp")
target_link_libraries(deadline_extractor_probe PRIVATE task17_extractor CURL::libcurl)
add_test(NAME task17_http_integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/deadline_http_test.py" "$<TARGET_FILE:deadline_extractor_probe>"
    "${REPO}/tests/fixtures/deadline_extractor" "${CMAKE_BINARY_DIR}/http_results.json")
add_test(NAME task17_live_criteria COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/live_result_checks_test.py"
    "${REPO}" "${CMAKE_BINARY_DIR}/criteria_results.json")
set_tests_properties(task17_unit task17_http_integration task17_live_criteria PROPERTIES TIMEOUT 180)

# TASK-021 chỉ link validator TASK-017; không phụ thuộc Scheduler/model/repository.
add_library(task21_confirmation STATIC "${REPO}/src/agent/deadline_confirmation.cpp")
target_link_libraries(task21_confirmation PUBLIC task17_extractor)
if(MSVC)
    target_compile_options(task21_confirmation PRIVATE /W4 /utf-8)
else()
    target_compile_options(task21_confirmation PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(deadline_confirmation_test deadline_confirmation_test.cpp)
target_link_libraries(deadline_confirmation_test PRIVATE task21_confirmation)
if(MSVC)
    target_compile_options(deadline_confirmation_test PRIVATE /W4 /utf-8)
else()
    target_compile_options(deadline_confirmation_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME task21_unit COMMAND deadline_confirmation_test "${CMAKE_BINARY_DIR}/task21_unit_results.json")
set_tests_properties(task21_unit PROPERTIES TIMEOUT 180)
