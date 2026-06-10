cmake_minimum_required(VERSION 3.16)
project(hesia_server_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HESIA_REPRODUCIBLE_BUILD "Enable reproducible-build path remapping flags" ON)
option(HESIA_ALLOW_SOFT_SIGN "Allow ML-DSA secret-key signing in the normal world (REE) for the server. Non-production only." OFF)
option(HESIA_ENABLE_HARDENING "Enable common hardening flags" ON)
option(HESIA_ENABLE_CFI "Enable Clang CFI/LTO hardening (Clang only)" OFF)
option(HESIA_ENABLE_SAFE_STACK "Enable Clang SafeStack when the target/runtime supports it" OFF)
option(HESIA_ENABLE_SHADOW_CALL_STACK "Enable Clang ShadowCallStack when the target/runtime supports it" OFF)
option(HESIA_ENABLE_LTO "Enable LTO (IPO) when supported" ON)
option(HESIA_STRIP_SYMBOLS "Strip symbols in Release builds" ON)
option(HESIA_SPLIT_DEBUG_INFO "Emit split debug info sidecars before stripping Release binaries" ON)
option(HESIA_ENABLE_RELEASE_CLOAKING "Enable additional reverse-cost measures for Release binaries" ON)

if(HESIA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HESIA_IPO_OK OUTPUT HESIA_IPO_MSG)
    if(NOT HESIA_IPO_OK)
        message(WARNING "LTO/IPO not supported: ${HESIA_IPO_MSG}")
    endif()
endif()

find_program(HESIA_OBJCOPY_EXECUTABLE NAMES objcopy llvm-objcopy)

if(HESIA_REPRODUCIBLE_BUILD AND NOT MSVC)
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=."
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_link_options("-Wl,--build-id=none")
    endif()
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

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

    find_path(TEEC_INCLUDE_DIRS
        NAMES tee_client_api.h
        PATHS /usr/include /usr/local/include
    )
    find_library(TEEC_LIBRARY
        NAMES teec
        PATHS /usr/lib /usr/local/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu
    )
    if(TEEC_INCLUDE_DIRS AND TEEC_LIBRARY)
        message(STATUS "OP-TEE client found: ${TEEC_LIBRARY}")
    else()
        if(HESIA_ALLOW_SOFT_SIGN)
            message(WARNING "OP-TEE client not found - continuing with HESIA_ALLOW_SOFT_SIGN local-server build")
            set(TEEC_INCLUDE_DIRS "")
            set(TEEC_LIBRARY "")
        else()
            message(FATAL_ERROR "OP-TEE client not found - required")
        endif()
    endif()
endif()

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
    src/telemetry_parser.cpp
    src/hesia_server_session.cpp

    ../drone_source/config.cpp
    ../drone_source/logger.cpp
    ../drone_source/security_utils.cpp
    ../drone_source/optee_client.cpp
    ../drone_source/crypto_real.cpp
    ../drone_source/serialization.cpp
    ../drone_source/secure_channel.cpp
    ../drone_source/video_channel.cpp
    ../drone_source/video_packet_parser.cpp

    ../drone_source/system_security.cpp
    ../drone_source/stack_protection.cpp
    ../drone_source/hardware_monitor.cpp
    ../drone_source/runtime_aslr.cpp
    ../drone_source/cfi_protection.cpp
    ../drone_source/clock_attack_protection.cpp
    ../drone_source/fault_injection_protection.cpp
    ../drone_source/em_attack_protection.cpp
    ../drone_source/voltage_glitch_protection.cpp
    ../drone_source/sandbox_linux.cpp
    ../drone_source/sandbox_windows.cpp
    ../drone_source/seccomp_bpf.cpp
)

target_include_directories(hesia_server_cpp PRIVATE
    include
    ../drone_source
    ${OPENSSL_INCLUDE_DIR}
    ${SECCOMP_INCLUDE_DIR}
    ${TEEC_INCLUDE_DIRS}
)

target_compile_definitions(hesia_server_cpp PRIVATE HESIA_POLICY_DIR=\"/etc/hesia/policy\")
if(HESIA_ALLOW_SOFT_SIGN)
    target_compile_definitions(hesia_server_cpp PRIVATE HESIA_ALLOW_SOFT_SIGN)
    message(WARNING "HESIA_ALLOW_SOFT_SIGN is enabled for the server: ML-DSA secret-key operations are allowed outside OP-TEE. Non-production only.")
endif()

if(LIBOQS_INCLUDE_DIRS AND LIBOQS_LIBRARY)
    target_include_directories(hesia_server_cpp PRIVATE ${LIBOQS_INCLUDE_DIRS})
    target_link_libraries(hesia_server_cpp PRIVATE ${LIBOQS_LIBRARY})
    target_compile_definitions(hesia_server_cpp PRIVATE HAVE_LIBOQS)
    message(STATUS "liboqs found - Dilithium/Kyber enabled")
else()
    message(FATAL_ERROR "liboqs not found - required")
endif()

if(MSVC)
    target_compile_options(hesia_server_cpp PRIVATE
        /W4 /O2 /std:c++17
        /guard:cf /GS /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /sdl
    )
    target_link_options(hesia_server_cpp PRIVATE
        /guard:cf /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT
    )
else()
    target_compile_options(hesia_server_cpp PRIVATE -Wall -Wextra -Wno-deprecated-declarations -O2)
    if(HESIA_ENABLE_HARDENING)
        target_compile_options(hesia_server_cpp PRIVATE
            -fstack-protector-strong
            -fstack-clash-protection
            -U_FORTIFY_SOURCE
            -D_FORTIFY_SOURCE=3
            -fPIE
            -ffunction-sections
            -fdata-sections
            -fno-semantic-interposition
            -fvisibility=hidden
            -fvisibility-inlines-hidden
        )
        target_link_options(hesia_server_cpp PRIVATE
            -pie
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
            -Wl,--gc-sections
        )
    endif()
    if(HESIA_ENABLE_RELEASE_CLOAKING)
        target_compile_options(hesia_server_cpp PRIVATE
            -fno-plt
            -fmerge-all-constants
            -fno-ident
        )
        target_link_options(hesia_server_cpp PRIVATE
            -Wl,--as-needed
            -Wl,--exclude-libs,ALL
            -Wl,-O2
        )
    endif()
    if(HESIA_ENABLE_CFI AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        find_program(HESIA_LLD_EXECUTABLE NAMES ld.lld lld)
        if(NOT HESIA_LLD_EXECUTABLE)
            message(FATAL_ERROR "Clang CFI build requires lld (ld.lld)")
        endif()
        target_compile_options(hesia_server_cpp PRIVATE
            -flto
            -fvisibility=hidden
            -fsanitize=cfi
        )
        target_link_options(hesia_server_cpp PRIVATE
            -flto
            -fuse-ld=lld
            -fsanitize=cfi
        )
        target_compile_options(hesia_server_cpp PRIVATE -DHESIA_CFI_BUILD=1)

        include(CheckCXXCompilerFlag)
        if(HESIA_ENABLE_SAFE_STACK)
            check_cxx_compiler_flag("-fsanitize=safe-stack" HESIA_SAFE_STACK_OK)
            if(HESIA_SAFE_STACK_OK)
                target_compile_options(hesia_server_cpp PRIVATE -fsanitize=safe-stack)
                target_link_options(hesia_server_cpp PRIVATE -fsanitize=safe-stack)
            else()
                message(FATAL_ERROR "HESIA_ENABLE_SAFE_STACK requested but unsupported by this Clang toolchain/runtime")
            endif()
        endif()

        if(HESIA_ENABLE_SHADOW_CALL_STACK)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|AARCH64)$")
                check_cxx_compiler_flag("-ffixed-x18 -fsanitize=shadow-call-stack" HESIA_SCS_OK)
                if(HESIA_SCS_OK)
                    target_compile_options(hesia_server_cpp PRIVATE -ffixed-x18 -fsanitize=shadow-call-stack)
                    target_link_options(hesia_server_cpp PRIVATE -ffixed-x18 -fsanitize=shadow-call-stack)
                else()
                    message(FATAL_ERROR "HESIA_ENABLE_SHADOW_CALL_STACK requested but unsupported by this Clang/aarch64 toolchain")
                endif()
            else()
                message(FATAL_ERROR "HESIA_ENABLE_SHADOW_CALL_STACK requires an aarch64 target")
            endif()
        endif()
    endif()
    if(HESIA_ENABLE_LTO AND HESIA_IPO_OK)
        set_property(TARGET hesia_server_cpp PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    if(HESIA_STRIP_SYMBOLS AND CMAKE_BUILD_TYPE STREQUAL "Release")
        if(CMAKE_STRIP AND HESIA_OBJCOPY_EXECUTABLE AND HESIA_SPLIT_DEBUG_INFO)
            add_custom_command(TARGET hesia_server_cpp POST_BUILD
                COMMAND bash "${CMAKE_SOURCE_DIR}/../tools/harden_release_binary.sh"
                        "$<TARGET_FILE:hesia_server_cpp>"
                        "$<TARGET_FILE_DIR:hesia_server_cpp>/debug"
                        "${CMAKE_STRIP}"
                        "${HESIA_OBJCOPY_EXECUTABLE}"
                        "hesia_server_cpp"
                COMMENT "Hardening and stripping hesia_server_cpp"
            )
        elseif(CMAKE_STRIP)
            add_custom_command(TARGET hesia_server_cpp POST_BUILD
                COMMAND "${CMAKE_STRIP}" --strip-all "$<TARGET_FILE:hesia_server_cpp>"
                COMMENT "Stripping symbols from hesia_server_cpp"
            )
        endif()
    endif()
endif()

target_link_libraries(hesia_server_cpp PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

if(TEEC_LIBRARY)
    target_link_libraries(hesia_server_cpp PRIVATE ${TEEC_LIBRARY})
endif()

if(SECCOMP_LIBRARY)
    target_link_libraries(hesia_server_cpp PRIVATE ${SECCOMP_LIBRARY})
    target_compile_definitions(hesia_server_cpp PRIVATE HAVE_LIBSECCOMP)
endif()

if(TEEC_LIBRARY)
    target_compile_definitions(hesia_server_cpp PRIVATE HAVE_OPTEE HESIA_HAVE_OPTEE)
endif()
