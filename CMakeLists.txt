cmake_minimum_required(VERSION 3.16)

# ══════════════════════════════════════════════════════════════════════════════
# Project Declaration — TASK-01 (LLMClient & Multimodal Vision)
# ══════════════════════════════════════════════════════════════════════════════
project(
    ai_agent
    VERSION     1.0.0
    DESCRIPTION "TASK-01: Ollama Multimodal Client PoC (OOP Assignment)"
    LANGUAGES   CXX
)

# ══════════════════════════════════════════════════════════════════════════════
# C++ Standard — C++17
# ══════════════════════════════════════════════════════════════════════════════
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Tự động nhận diện thư viện trong MSYS2 UCRT64 / MINGW64 trên Windows nếu có
if(WIN32)
    if(EXISTS "C:/msys64/ucrt64")
        list(APPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
    endif()
    if(EXISTS "C:/msys64/mingw64")
        list(APPEND CMAKE_PREFIX_PATH "C:/msys64/mingw64")
    endif()
endif()

# Tìm kiếm thư viện libcurl
find_package(CURL REQUIRED)

# Thư mục include (Header-only nlohmann/json.hpp có sẵn trong include/nlohmann/)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CURL_INCLUDE_DIRS}
)

# ══════════════════════════════════════════════════════════════════════════════
# Executable: ai_agent (Bản thử nghiệm API TASK-01: Text & Vision)
# ══════════════════════════════════════════════════════════════════════════════
add_executable(ai_agent
    src/main.cpp
    src/client/ollama_client.cpp
)

target_link_libraries(ai_agent PRIVATE
    CURL::libcurl
)

if(WIN32)
    target_link_libraries(ai_agent PRIVATE
        ws2_32
        wldap32
        crypt32
    )
endif()
