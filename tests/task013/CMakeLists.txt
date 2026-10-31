cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(task013_checks LANGUAGES CXX)
    include(CTest)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(CURL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Tạo thư viện parser độc lập để các executable dùng chung phần parse JSON.
add_library(task13_parser STATIC "${REPO}/src/client/tool_parser.cpp")
target_include_directories(task13_parser PUBLIC "${REPO}/include")
if(MSVC)
    target_compile_options(task13_parser PRIVATE /W4 /utf-8)
else()
    target_compile_options(task13_parser PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# Ghép HTTP Client với API tool calling và liên kết parser cùng libcurl.
add_library(task13_client STATIC
    "${REPO}/src/client/ollama_client.cpp" "${REPO}/src/client/ollama_tool_chat.cpp")
target_include_directories(task13_client PUBLIC "${REPO}/include")
target_link_libraries(task13_client PUBLIC task13_parser CURL::libcurl)

# Tạo và đăng ký test parser với thư mục fixture và file báo cáo JSON.
add_executable(tool_parser_test "${REPO}/tests/tool_parser_test.cpp")
target_link_libraries(tool_parser_test PRIVATE task13_parser)
add_test(NAME task13_parser COMMAND tool_parser_test "${REPO}/assets/task013" "${CMAKE_BINARY_DIR}/parser_results.json")

# Tạo CLI probe và đăng ký mock HTTP để kiểm tra request của Client C++.
add_executable(tool_client_probe "${REPO}/tests/tool_client_probe.cpp")
target_link_libraries(tool_client_probe PRIVATE task13_client)
add_test(NAME task13_client_transport COMMAND "${Python3_EXECUTABLE}"
    "${REPO}/tests/tool_client_http_test.py" "$<TARGET_FILE:tool_client_probe>"
    "${REPO}/assets/task013" "${CMAKE_BINARY_DIR}/client_results.json")
