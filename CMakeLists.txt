cmake_minimum_required(VERSION 3.16)
project(veda_rtsp
    VERSION 0.1.0
    DESCRIPTION "From-scratch RTSP/RTP server on embedded Linux"
    LANGUAGES CXX
)

# ---- Standard ---------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Warnings ---------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# Default to Release if user did not specify
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Core library -----------------------------------------------------------
# Building source as a static lib lets tests link against the same code
# without recompiling. The executable is just a thin main().
add_library(veda_rtsp_core STATIC
    src/rtsp/server.cpp
    src/rtsp/parser.cpp
    src/rtsp/response.cpp
    src/rtsp/session.cpp
    src/sdp/builder.cpp
    src/rtp/packetizer.cpp
    src/rtp/h264_fua.cpp
    src/rtp/streamer.cpp
    src/net/tcp_socket.cpp
    src/net/udp_socket.cpp
    src/camera/source.cpp
)

target_include_directories(veda_rtsp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---- Executable -------------------------------------------------------------
add_executable(veda_rtsp src/main.cpp)
target_link_libraries(veda_rtsp PRIVATE veda_rtsp_core)

# ---- Tests ------------------------------------------------------------------
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
