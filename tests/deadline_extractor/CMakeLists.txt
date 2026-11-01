cmake_minimum_required(VERSION 3.20)
project(task017_deadline_extractor LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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
