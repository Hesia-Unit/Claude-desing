cmake_minimum_required(VERSION 3.20)
project(hesia_unit_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(hesia_secure_channel_tests
    test_secure_channel.cpp
    test_support_stubs.cpp
    ../../drone_source/secure_channel.cpp
)

target_include_directories(hesia_secure_channel_tests PRIVATE
    ../../drone_source
)

target_compile_options(hesia_secure_channel_tests PRIVATE
    -Wall
    -Wextra
    -Wno-deprecated-declarations
    -O2
)

target_link_libraries(hesia_secure_channel_tests PRIVATE
    OpenSSL::Crypto
    Threads::Threads
)
