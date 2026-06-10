cmake_minimum_required(VERSION 3.20)
project(hesia_fuzzers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HESIA_REPRODUCIBLE_BUILD "Enable reproducible-build flags" ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "LibFuzzer targets require Clang/clang++")
endif()

add_compile_options(
    -O1
    -g
    -fno-omit-frame-pointer
    -fsanitize=fuzzer,address,undefined
    -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
)
add_link_options(-fsanitize=fuzzer,address,undefined)

if(HESIA_REPRODUCIBLE_BUILD AND NOT MSVC)
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=."
    )
    add_link_options("-Wl,--build-id=none")
endif()

add_executable(hesia_fuzz_serialization
    fuzz_serialization.cpp
    ../../drone_source/serialization.cpp
)
target_include_directories(hesia_fuzz_serialization PRIVATE
    ../../drone_source
)

add_executable(hesia_fuzz_video_packet
    fuzz_video_packet.cpp
    ../../drone_source/video_packet_parser.cpp
)
target_include_directories(hesia_fuzz_video_packet PRIVATE
    ../../drone_source
)

add_executable(hesia_fuzz_telemetry
    fuzz_telemetry.cpp
    ../../server_source/src/telemetry_parser.cpp
)
target_include_directories(hesia_fuzz_telemetry PRIVATE
    ../../server_source/include
)
