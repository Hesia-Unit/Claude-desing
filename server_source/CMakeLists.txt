cmake_minimum_required(VERSION 3.16)
project(hesia_server_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# libseccomp (required)
if(NOT WIN32)
    find_path(SECCOMP_INCLUDE_DIR
        NAMES seccomp.h
        PATHS /usr/include /usr/local/include
    )
    find_library(SECCOMP_LIBRARY
        NAMES seccomp
        PATHS /usr/lib /usr/local/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu
    )
    if(SECCOMP_INCLUDE_DIR AND SECCOMP_LIBRARY)
        message(STATUS "libseccomp found: ${SECCOMP_LIBRARY}")
    else()
        message(FATAL_ERROR "libseccomp not found - required")
    endif()
endif()

# liboqs (Kyber/Dilithium via OQS) - Même recherche que le drone
set(LIBOQS_ROOT_DIR "$ENV{HOME}/liboqs" CACHE PATH "liboqs root directory")
set(LIBOQS_PATHS
    ${LIBOQS_ROOT_DIR}
    /usr/local/liboqs
    /opt/liboqs
    $ENV{LIBOQS_ROOT}
)

find_path(LIBOQS_INCLUDE_DIRS
    NAMES oqs/oqs.h
    PATHS ${LIBOQS_PATHS}
    PATH_SUFFIXES include build/include src
)

find_library(LIBOQS_LIBRARY
    NAMES oqs
    PATHS ${LIBOQS_PATHS}
    PATH_SUFFIXES lib build/lib .libs
)

add_executable(hesia_server_cpp
    src/main.cpp
    src/net_framing.cpp
    src/tls_utils.cpp
    src/security_audit.cpp
    src/policy.cpp
    src/policy_ed25519_public_key.cpp
    src/hesia_server_session.cpp

    # Reuse protocol/crypto/channel code from the Drone implementation to maximize alignment
    ../../../IMPLEMENTATION/config.cpp
    ../../../IMPLEMENTATION/logger.cpp
    ../../../IMPLEMENTATION/security_utils.cpp
    ../../../IMPLEMENTATION/crypto_real.cpp
    ../../../IMPLEMENTATION/serialization.cpp
    ../../../IMPLEMENTATION/secure_channel.cpp
    ../../../IMPLEMENTATION/video_channel.cpp
    
    # Security modules (required by security_utils.cpp)
    ../../../IMPLEMENTATION/system_security.cpp
    ../../../IMPLEMENTATION/stack_protection.cpp
    ../../../IMPLEMENTATION/hardware_monitor.cpp
    ../../../IMPLEMENTATION/runtime_aslr.cpp
    ../../../IMPLEMENTATION/cfi_protection.cpp
    ../../../IMPLEMENTATION/sandbox_linux.cpp
    ../../../IMPLEMENTATION/sandbox_windows.cpp
    ../../../IMPLEMENTATION/seccomp_bpf.cpp
)

target_include_directories(hesia_server_cpp PRIVATE
    include
    ../../../IMPLEMENTATION
    ${OPENSSL_INCLUDE_DIR}
    ${SECCOMP_INCLUDE_DIR}
)

target_compile_definitions(hesia_server_cpp PRIVATE HESIA_POLICY_DIR=\"/etc/hesia/policy\")

# Ajouter les includes et librairies liboqs seulement si trouvées
if(LIBOQS_INCLUDE_DIRS AND LIBOQS_LIBRARY)
    target_include_directories(hesia_server_cpp PRIVATE ${LIBOQS_INCLUDE_DIRS})
    target_link_libraries(hesia_server_cpp PRIVATE ${LIBOQS_LIBRARY})
    target_compile_definitions(hesia_server_cpp PRIVATE HAVE_LIBOQS)
    message(STATUS "liboqs found - Dilithium/Kyber enabled")
else()
    message(FATAL_ERROR "liboqs not found - required")
endif()

target_compile_options(hesia_server_cpp PRIVATE)

target_link_libraries(hesia_server_cpp PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

if(SECCOMP_LIBRARY)
    target_link_libraries(hesia_server_cpp PRIVATE ${SECCOMP_LIBRARY})
    target_compile_definitions(hesia_server_cpp PRIVATE HAVE_LIBSECCOMP)
endif()
