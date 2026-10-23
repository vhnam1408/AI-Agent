cmake_minimum_required(VERSION 3.20)

# ══════════════════════════════════════════════════════════════════════════════
# Project: EduSchedule-Agent (Milestone G1: Architecture Freeze - TASK-05)
# ══════════════════════════════════════════════════════════════════════════════
project(
    ai_agent
    VERSION     1.0.0
    DESCRIPTION "EduSchedule-Agent: Milestone G1 - Architecture Freeze (TASK-05)"
    LANGUAGES   CXX
)

# ══════════════════════════════════════════════════════════════════════════════
# C++ Standard — C++23 (Backward-compatible with C++20)
# ══════════════════════════════════════════════════════════════════════════════
set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# TASK-09 dùng std::expected. Báo sớm nếu compiler/thư viện C++ còn quá cũ.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>
int main() { std::expected<int, int> result = 1; return result.value() - 1; }"
    HAS_STD_EXPECTED)
if(NOT HAS_STD_EXPECTED)
    message(FATAL_ERROR "TASK-09 requires a compiler and standard library supporting C++23 std::expected.")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# MSYS2 / UCRT64 prefix paths on Windows
if(WIN32)
    if(EXISTS "C:/msys64/ucrt64")
        list(APPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
    endif()
    if(EXISTS "C:/msys64/mingw64")
        list(APPEND CMAKE_PREFIX_PATH "C:/msys64/mingw64")
    endif()
endif()

# ══════════════════════════════════════════════════════════════════════════════
# Dependencies: CURL & SQLite3
# ══════════════════════════════════════════════════════════════════════════════
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# ══════════════════════════════════════════════════════════════════════════════
# Include Directories: include/ and src/
# ══════════════════════════════════════════════════════════════════════════════
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CURL_INCLUDE_DIRS}
)

if(SQLite3_FOUND)
    include_directories(${SQLite3_INCLUDE_DIRS})
endif()

# ══════════════════════════════════════════════════════════════════════════════
# Executable Target: ai_agent
# ══════════════════════════════════════════════════════════════════════════════
# Khung Agent dùng chung cho chương trình và kiểm thử mock (không cần Ollama).
add_library(agent_core STATIC src/agent/agent_loop.cpp)
target_include_directories(agent_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(ai_agent
    src/main.cpp
    src/client/ollama_client.cpp
    src/tools/memory_tool.cpp
)

target_link_libraries(ai_agent PRIVATE
    agent_core
    CURL::libcurl
)

if(SQLite3_FOUND)
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(ai_agent PRIVATE SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
        target_link_libraries(ai_agent PRIVATE SQLite::SQLite3)
    else()
        target_link_libraries(ai_agent PRIVATE ${SQLite3_LIBRARIES})
    endif()
endif()

if(WIN32)
    target_link_libraries(ai_agent PRIVATE
        ws2_32
        wldap32
        crypt32
    )
endif()

# ══════════════════════════════════════════════════════════════════════════════
# Tên chạy cũ; Windows có đuôi .exe, Linux/Kaggle không có đuôi.
# ══════════════════════════════════════════════════════════════════════════════
add_custom_command(TARGET ai_agent POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:ai_agent>
    ${CMAKE_BINARY_DIR}/edu_schedule_poc${CMAKE_EXECUTABLE_SUFFIX}
    COMMENT "Generating edu_schedule_poc alias"
)

# Kiểm thử Client TASK-01; giữ nguyên cấu hình build của các task đã có.
include(CTest)
if(BUILD_TESTING)
    add_executable(agent_contract_test tests/agent_contract_test.cpp)
    target_link_libraries(agent_contract_test PRIVATE agent_core)
    add_test(NAME agent_contract COMMAND agent_contract_test)

    add_executable(memory_tool_test
        tests/memory_tool_test.cpp
        src/tools/memory_tool.cpp
    )
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(memory_tool_test PRIVATE SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
        target_link_libraries(memory_tool_test PRIVATE SQLite::SQLite3)
    else()
        target_link_libraries(memory_tool_test PRIVATE ${SQLite3_LIBRARIES})
    endif()
    add_test(NAME memory_tool COMMAND memory_tool_test)

    add_executable(ollama_client_smoke_test
        tests/ollama_client_smoke_test.cpp
        src/client/ollama_client.cpp
    )
    target_link_libraries(ollama_client_smoke_test PRIVATE CURL::libcurl)
    add_test(NAME ollama_client COMMAND ollama_client_smoke_test)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ollama_http
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ollama_http_test.py
                $<TARGET_FILE:ai_agent> $<TARGET_FILE:ollama_client_smoke_test>)
    endif()
endif()
