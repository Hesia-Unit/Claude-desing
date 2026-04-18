cmake_minimum_required(VERSION 3.15)
project(HESIA_DRONE_CPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== OPTIONS =====
option(HESIA_FIPS_BUILD "Build FIPS-ready version (disable PQC)" OFF)
option(HESIA_USE_FIPS_MODULE "Use FIPS module wrapper" OFF)
option(HESIA_ENABLE_HARDENING "Enable common hardening flags" ON)
option(HESIA_ENABLE_CFI "Enable Clang CFI/LTO hardening (Clang only)" OFF)
option(HESIA_ENABLE_LTO "Enable LTO (IPO) when supported" ON)
option(HESIA_STRIP_SYMBOLS "Strip symbols in Release builds" ON)
option(HESIA_ENABLE_SENTINEL "Build HESIA Sentinel (Ada) module" ON)
option(HESIA_DISABLE_CFI_OPENCV "Disable CFI on OpenCV modules (workaround for CFI traps)" OFF)

if(HESIA_FIPS_BUILD)
    message(FATAL_ERROR "FIPS build not allowed: PQC/liboqs is mandatory for this build")
endif()
if(HESIA_USE_FIPS_MODULE)
    message(STATUS "Using FIPS module wrapper")
endif()

# ===== LTO SUPPORT =====
if(HESIA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HESIA_IPO_OK OUTPUT HESIA_IPO_MSG)
    if(NOT HESIA_IPO_OK)
        message(WARNING "LTO/IPO not supported: ${HESIA_IPO_MSG}")
    endif()
endif()

# ===== DEPENDENCIES =====
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

# ===== COMMON PATHS (CUDA/TensorRT) =====
set(CUDA_PATHS
    /usr/local/cuda
    /usr/local/cuda-12.6 /usr/local/cuda-12.5 /usr/local/cuda-12.4 /usr/local/cuda-12.3
    /usr/local/cuda-12.2 /usr/local/cuda-12.1 /usr/local/cuda-12.0
    /usr/local/cuda-11.8 /usr/local/cuda-11.7 /usr/local/cuda-11.6
    /opt/cuda
    $ENV{CUDA_HOME} $ENV{CUDA_PATH}
)

set(TENSORRT_PATHS
    /usr/local/tensorrt
    /usr/lib/aarch64-linux-gnu
    /usr/lib/x86_64-linux-gnu
    /opt/TensorRT
    $ENV{TENSORRT_HOME} $ENV{TENSORRT_PATH}
)

# ===== CUDA detection (runtime only: cudart) =====
find_path(CUDA_INCLUDE_DIRS
    NAMES cuda_runtime_api.h
    PATHS ${CUDA_PATHS}
    PATH_SUFFIXES include
)

find_library(CUDA_CUDART_LIBRARY
    NAMES cudart
    PATHS ${CUDA_PATHS}
    PATH_SUFFIXES lib64 lib
)

set(HAVE_CUDA FALSE)
if(CUDA_INCLUDE_DIRS AND CUDA_CUDART_LIBRARY)
    set(HAVE_CUDA TRUE)
    message(STATUS "CUDA found: ${CUDA_INCLUDE_DIRS}")
else()
    message(WARNING "CUDA not found - CPU only")
    set(CUDA_INCLUDE_DIRS "")
    set(CUDA_CUDART_LIBRARY "")
endif()

# ===== TensorRT detection =====
find_path(TENSORRT_INCLUDE_DIRS
    NAMES NvInfer.h
    PATHS ${TENSORRT_PATHS}
    PATH_SUFFIXES include
)

find_library(TENSORRT_NVINFER_LIBRARY
    NAMES nvinfer
    PATHS ${TENSORRT_PATHS}
    PATH_SUFFIXES lib lib64
)

find_library(TENSORRT_PARSER_LIBRARY
    NAMES nvonnxparser
    PATHS ${TENSORRT_PATHS}
    PATH_SUFFIXES lib lib64
)

set(HAVE_TENSORRT FALSE)
if(TENSORRT_INCLUDE_DIRS AND TENSORRT_NVINFER_LIBRARY)
    set(HAVE_TENSORRT TRUE)
    message(STATUS "TensorRT found: ${TENSORRT_INCLUDE_DIRS}")

    # (Optional) read version
    if(EXISTS "${TENSORRT_INCLUDE_DIRS}/NvInferVersion.h")
        file(READ "${TENSORRT_INCLUDE_DIRS}/NvInferVersion.h" _tver)
        string(REGEX MATCH "#define NV_TENSORRT_MAJOR ([0-9]+)" _ ${_tver})
        set(TENSORRT_MAJOR ${CMAKE_MATCH_1})
        string(REGEX MATCH "#define NV_TENSORRT_MINOR ([0-9]+)" _ ${_tver})
        set(TENSORRT_MINOR ${CMAKE_MATCH_1})
        message(STATUS "TensorRT version: ${TENSORRT_MAJOR}.${TENSORRT_MINOR}")
    endif()
else()
    message(WARNING "TensorRT not found - YOLO/MiDaS sources will not be compiled")
    set(TENSORRT_INCLUDE_DIRS "")
    set(TENSORRT_NVINFER_LIBRARY "")
    set(TENSORRT_PARSER_LIBRARY "")
endif()

# ===== libseccomp detection (optional) =====
set(HAVE_SECCOMP FALSE)
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
        set(HAVE_SECCOMP TRUE)
        message(STATUS "libseccomp found: ${SECCOMP_LIBRARY}")
    else()
        message(FATAL_ERROR "libseccomp not found - required")
    endif()
endif()

# ===== OP-TEE client detection (optional) =====
set(HAVE_OPTEE FALSE)
if(NOT WIN32)
    find_path(TEEC_INCLUDE_DIRS
        NAMES tee_client_api.h
        PATHS /usr/include /usr/local/include
    )
    find_library(TEEC_LIBRARY
        NAMES teec
        PATHS /usr/lib /usr/local/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu
    )
    if(TEEC_INCLUDE_DIRS AND TEEC_LIBRARY)
        set(HAVE_OPTEE TRUE)
        message(STATUS "OP-TEE client found: ${TEEC_LIBRARY}")
    else()
        message(FATAL_ERROR "OP-TEE client not found - required")
    endif()
endif()

# ===== SOURCES =====
set(SOURCES
    logger.cpp
    security_audit.cpp
    kdf_sp800_108.cpp
    policy.cpp
    policy_ed25519_public_key.cpp
    optee_client.cpp
    crypto_real.cpp
    tls_channel.cpp
    secure_channel.cpp
    video_channel.cpp
    video_manager.cpp
    drone_network.cpp
    error_handler.cpp
    clean_pipeline.cpp
    hesia_drone.cpp
    mldsa87_public_key.cpp
    policy_pqc_public_key.cpp
    serialization.cpp
    main.cpp
    security_utils.cpp
    config.cpp
    runtime_aslr.cpp
    stack_protection.cpp
    cfi_protection.cpp
    system_security.cpp
    hardware_monitor.cpp
    fuzzing_framework.cpp
    clock_attack_protection.cpp
    fault_injection_protection.cpp
    em_attack_protection.cpp
    voltage_glitch_protection.cpp
    sentinel_bridge.cpp
)

# TensorRT-dependent sources
if(HAVE_TENSORRT)
    list(APPEND SOURCES
        yolo_processor.cpp
        midas_processor.cpp
    )
endif()

# Disable CFI only for TensorRT modules (Clang CFI can break on external DSOs)
if(HAVE_TENSORRT AND HESIA_ENABLE_CFI AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        yolo_processor.cpp
        midas_processor.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=cfi"
    )
endif()

# Optional: disable CFI on OpenCV-heavy modules if runtime traps occur
if(HESIA_DISABLE_CFI_OPENCV AND HESIA_ENABLE_CFI AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        video_manager.cpp
        clean_pipeline.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=cfi"
    )
endif()

# seccomp-dependent source (mandatory)
list(APPEND SOURCES seccomp_bpf.cpp)

# FIPS module sources
if(HESIA_USE_FIPS_MODULE)
    list(APPEND SOURCES
        fips_module.cpp
        fips_selftest.cpp
    )
endif()

# Platform-specific source
if(WIN32)
    list(APPEND SOURCES sandbox_windows.cpp)
else()
    list(APPEND SOURCES sandbox_linux.cpp)
endif()

add_executable(hesia_drone ${SOURCES})

# ===== SENTINEL (Ada static library) =====
if(HESIA_ENABLE_SENTINEL)
    find_program(GPRBUILD_EXECUTABLE gprbuild)
    if(NOT GPRBUILD_EXECUTABLE)
        message(FATAL_ERROR "gprbuild not found - required for Sentinel")
    endif()

    find_library(GNAT_LIBRARY gnat)
    find_library(GNARL_LIBRARY gnarl)
    if(NOT GNAT_LIBRARY OR NOT GNARL_LIBRARY)
        message(FATAL_ERROR "GNAT runtime libraries not found (gnat/gnarl)")
    endif()

    set(SENTINEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Sentinel")
    set(SENTINEL_GPR "${SENTINEL_DIR}/hesia_sentinel.gpr")
    set(SENTINEL_LIB "${SENTINEL_DIR}/lib/libhesia_sentinel.so")

    add_custom_command(
        OUTPUT "${SENTINEL_LIB}"
        COMMAND "${GPRBUILD_EXECUTABLE}" -p -P "${SENTINEL_GPR}" -cargs -fno-pie
        WORKING_DIRECTORY "${SENTINEL_DIR}"
        DEPENDS
            "${SENTINEL_GPR}"
            "${SENTINEL_DIR}/hesia-sentinel_api.ads"
            "${SENTINEL_DIR}/hesia-sentinel_api.adb"
            "${SENTINEL_DIR}/hesia-sentinel_core.ads"
            "${SENTINEL_DIR}/hesia-sentinel_core.adb"
            "${SENTINEL_DIR}/hesia-sentinel_hash.ads"
            "${SENTINEL_DIR}/hesia-sentinel_hash.adb"
            "${SENTINEL_DIR}/hesia-sentinel_hw.ads"
            "${SENTINEL_DIR}/hesia-sentinel_hw.adb"
            "${SENTINEL_DIR}/hesia-sentinel_config.ads"
            "${SENTINEL_DIR}/hesia-sentinel_config.adb"
            "${SENTINEL_DIR}/hesia-sentinel_util.ads"
            "${SENTINEL_DIR}/hesia-sentinel_util.adb"
        COMMENT "Building HESIA Sentinel (Ada static library)"
    )

    add_custom_target(hesia_sentinel_lib ALL DEPENDS "${SENTINEL_LIB}")
    add_dependencies(hesia_drone hesia_sentinel_lib)

    add_library(hesia_sentinel SHARED IMPORTED GLOBAL)
    set_target_properties(hesia_sentinel PROPERTIES IMPORTED_LOCATION "${SENTINEL_LIB}")

    if(NOT WIN32)
        # Ensure runtime can find the Sentinel shared library
        set_target_properties(hesia_drone PROPERTIES
            BUILD_RPATH "${SENTINEL_DIR}/lib"
            INSTALL_RPATH "${SENTINEL_DIR}/lib"
        )
    endif()
endif()

target_compile_definitions(hesia_drone PRIVATE HESIA_POLICY_DIR=\"/etc/hesia/policy\")

# ===== INCLUDE DIRS =====
target_include_directories(hesia_drone PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIRS}
    ${TENSORRT_INCLUDE_DIRS}
    ${SECCOMP_INCLUDE_DIR}
    ${TEEC_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ===== LINK =====
if(WIN32)
    target_link_libraries(hesia_drone
        ${OpenCV_LIBS}
        OpenSSL::SSL OpenSSL::Crypto
        spdlog::spdlog
        Threads::Threads
        ws2_32
        dbghelp psapi userenv advapi32 kernel32
    )
else()
    target_link_libraries(hesia_drone
        ${OpenCV_LIBS}
        OpenSSL::SSL OpenSSL::Crypto
        spdlog::spdlog
        Threads::Threads
        rt dl
    )

    if(HAVE_CUDA)
        target_link_libraries(hesia_drone ${CUDA_CUDART_LIBRARY})
        target_compile_definitions(hesia_drone PRIVATE HAVE_CUDA)
    endif()

    if(HAVE_TENSORRT)
        target_link_libraries(hesia_drone ${TENSORRT_NVINFER_LIBRARY})
        if(TENSORRT_PARSER_LIBRARY)
            target_link_libraries(hesia_drone ${TENSORRT_PARSER_LIBRARY})
        endif()
        target_compile_definitions(hesia_drone PRIVATE HAVE_TENSORRT)
    endif()

    if(HAVE_SECCOMP)
        target_link_libraries(hesia_drone ${SECCOMP_LIBRARY})
        target_compile_definitions(hesia_drone PRIVATE HAVE_SECCOMP HAVE_LIBSECCOMP)
    endif()

    if(HAVE_OPTEE)
        target_link_libraries(hesia_drone ${TEEC_LIBRARY})
        target_compile_definitions(hesia_drone PRIVATE HESIA_HAVE_OPTEE)
    endif()

    if(HESIA_ENABLE_SENTINEL)
        target_link_libraries(hesia_drone hesia_sentinel ${GNAT_LIBRARY} ${GNARL_LIBRARY})
    endif()
endif()

# ===== DEFINITIONS (FIPS) =====
if(HESIA_FIPS_BUILD)
    target_compile_definitions(hesia_drone PRIVATE HESIA_FIPS_BUILD HESIA_DISABLE_PQC)
endif()
if(HESIA_USE_FIPS_MODULE)
    target_compile_definitions(hesia_drone PRIVATE HESIA_USE_FIPS_MODULE)
endif()

# ===== liboqs (PQC) =====
if(NOT HESIA_FIPS_BUILD)
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

    if(LIBOQS_INCLUDE_DIRS AND LIBOQS_LIBRARY)
        target_include_directories(hesia_drone PRIVATE ${LIBOQS_INCLUDE_DIRS})
        target_link_libraries(hesia_drone ${LIBOQS_LIBRARY})
        target_compile_definitions(hesia_drone PRIVATE HAVE_LIBOQS)
        message(STATUS "liboqs found - Dilithium/Kyber enabled")
    else()
        message(FATAL_ERROR "liboqs not found - required")
    endif()
endif()

# ===== HARDENING FLAGS =====
if(MSVC)
    target_compile_options(hesia_drone PRIVATE
        /W4 /O2 /std:c++17
        /guard:cf /GS /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /sdl
    )
    target_link_options(hesia_drone PRIVATE
        /guard:cf /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT
    )
else()
    target_compile_options(hesia_drone PRIVATE -Wall -Wextra -Wno-deprecated-declarations -O2)

    if(HESIA_ENABLE_HARDENING)
        target_compile_options(hesia_drone PRIVATE
            -fstack-protector-strong
            -fstack-clash-protection
            -D_FORTIFY_SOURCE=2
            -fPIE
            -ffunction-sections
            -fdata-sections
            -fno-semantic-interposition
            -fvisibility=hidden
            -fvisibility-inlines-hidden
        )
        target_link_options(hesia_drone PRIVATE
            -pie
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
            -Wl,--gc-sections
        )
    endif()

    # Optional: Clang CFI (OFF by default because it can be fragile)
    if(HESIA_ENABLE_CFI AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(hesia_drone PRIVATE
            -flto
            -fvisibility=hidden
            -fsanitize=cfi
        )
        target_link_options(hesia_drone PRIVATE
            -flto
            -fsanitize=cfi
        )
        # Use explicit -D to avoid generator quirks in some compile databases
        target_compile_options(hesia_drone PRIVATE -DHESIA_CFI_BUILD=1)
    endif()

    if(HESIA_ENABLE_LTO AND HESIA_IPO_OK)
        set_property(TARGET hesia_drone PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    if(HESIA_STRIP_SYMBOLS AND CMAKE_BUILD_TYPE STREQUAL "Release")
        if(CMAKE_STRIP)
            add_custom_command(TARGET hesia_drone POST_BUILD
                COMMAND "${CMAKE_STRIP}" "$<TARGET_FILE:hesia_drone>"
                COMMENT "Stripping symbols from hesia_drone"
            )
        endif()
    endif()
endif()

# ===== SUMMARY =====
message(STATUS "=========================================")
message(STATUS "Configuration HESIA_DRONE_CPP")
message(STATUS "=========================================")
message(STATUS "OpenCV: ${OpenCV_VERSION}")
message(STATUS "OpenSSL: ${OpenSSL_VERSION}")
message(STATUS "CUDA: ${HAVE_CUDA} (${CUDA_INCLUDE_DIRS})")
message(STATUS "TensorRT: ${HAVE_TENSORRT} (${TENSORRT_INCLUDE_DIRS})")
message(STATUS "seccomp: ${HAVE_SECCOMP}")
message(STATUS "OP-TEE: ${HAVE_OPTEE}")
message(STATUS "FIPS build: ${HESIA_FIPS_BUILD}")
message(STATUS "=========================================")
