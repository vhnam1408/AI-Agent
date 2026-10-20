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
find_package(SQLite3 QUIET)

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
add_executable(ai_agent
    src/main.cpp
    src/client/ollama_client.cpp
)

target_link_libraries(ai_agent PRIVATE
    CURL::libcurl
)

if(SQLite3_FOUND)
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(ai_agent PRIVATE SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
        target_link_libraries(ai_agent PRIVATE SQLite::SQLite3)
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
# Alias executable: edu_schedule_poc (backward-compatible with TASK-01)
# ══════════════════════════════════════════════════════════════════════════════
add_custom_command(TARGET ai_agent POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:ai_agent>
    ${CMAKE_BINARY_DIR}/edu_schedule_poc.exe
    COMMENT "Generating edu_schedule_poc.exe alias"
)

# Kiểm thử Client TASK-01; giữ nguyên cấu hình build của các task đã có.
include(CTest)
if(BUILD_TESTING)
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
