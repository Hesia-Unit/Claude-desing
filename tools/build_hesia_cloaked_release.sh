#!/usr/bin/env bash
set -euo pipefail
umask 077

if [ $# -lt 3 ]; then
  echo "Usage: $0 <workspace_root> <drone_build_dir> <server_build_dir>" >&2
  exit 2
fi

workspace_root="$1"
drone_build_dir="$2"
server_build_dir="$3"

python_bin="${PYTHON_BIN:-python3}"
build_type="${HESIA_BUILD_TYPE:-Release}"
cmake_bin="${CMAKE_BIN:-cmake}"
drone_compiler="${HESIA_DRONE_CXX:-}"
drone_enable_cfi="${HESIA_DRONE_ENABLE_CFI:-0}"
drone_disable_cfi_opencv="${HESIA_DRONE_DISABLE_CFI_OPENCV:-0}"
drone_liboqs_root="${HESIA_DRONE_LIBOQS_ROOT:-}"
server_liboqs_root="${HESIA_SERVER_LIBOQS_ROOT:-}"
source_date_epoch="${SOURCE_DATE_EPOCH:-}"

if [ -z "$source_date_epoch" ] && command -v git >/dev/null 2>&1; then
  source_date_epoch="$(git -C "$workspace_root" log -1 --format=%ct 2>/dev/null || true)"
fi

if [ -n "$source_date_epoch" ]; then
  export SOURCE_DATE_EPOCH="$source_date_epoch"
fi

mkdir -p "$drone_build_dir" "$server_build_dir"

drone_cmake_args=(
  -S "${workspace_root}/drone_source"
  -B "$drone_build_dir"
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DHESIA_ENABLE_HARDENING=ON
  -DHESIA_ENABLE_LTO=ON
  -DHESIA_ENABLE_RELEASE_CLOAKING=ON
  -DHESIA_STRIP_SYMBOLS=ON
  -DHESIA_SPLIT_DEBUG_INFO=ON
  -DHESIA_REPRODUCIBLE_BUILD=ON
)

if [ -n "$drone_compiler" ]; then
  drone_cmake_args+=(-DCMAKE_CXX_COMPILER="$drone_compiler")
fi

if [ "$drone_enable_cfi" = "1" ]; then
  drone_cmake_args+=(-DHESIA_ENABLE_CFI=ON)
fi

if [ "$drone_disable_cfi_opencv" = "1" ]; then
  drone_cmake_args+=(-DHESIA_DISABLE_CFI_OPENCV=ON)
fi

if [ -n "$drone_liboqs_root" ]; then
  drone_cmake_args+=(-DLIBOQS_ROOT_DIR="$drone_liboqs_root")
fi

"$cmake_bin" "${drone_cmake_args[@]}"

"$cmake_bin" --build "$drone_build_dir" --config "${build_type}" -j"$(nproc)"

server_cmake_args=(
  -S "${workspace_root}/server_source"
  -B "$server_build_dir"
  -DCMAKE_BUILD_TYPE="${build_type}"
  -DHESIA_ENABLE_HARDENING=ON
  -DHESIA_ENABLE_LTO=ON
  -DHESIA_ENABLE_RELEASE_CLOAKING=ON
  -DHESIA_STRIP_SYMBOLS=ON
  -DHESIA_SPLIT_DEBUG_INFO=ON
  -DHESIA_REPRODUCIBLE_BUILD=ON
)

if [ -n "$server_liboqs_root" ]; then
  server_cmake_args+=(-DLIBOQS_ROOT_DIR="$server_liboqs_root")
fi

"$cmake_bin" "${server_cmake_args[@]}"

"$cmake_bin" --build "$server_build_dir" --config "${build_type}" -j"$(nproc)"

mkdir -p "${workspace_root}/artifacts/release_metrics"
"$python_bin" "${workspace_root}/tools/measure_release_artifact.py" \
  "${drone_build_dir}/hesia_drone" \
  --out "${workspace_root}/artifacts/release_metrics/hesia_drone.json"

"$python_bin" "${workspace_root}/tools/measure_release_artifact.py" \
  "${server_build_dir}/hesia_server_cpp" \
  --out "${workspace_root}/artifacts/release_metrics/hesia_server_cpp.json"

mkdir -p "${workspace_root}/artifacts"
"$python_bin" "${workspace_root}/tools/generate_firmware_sbom.py" \
  --repo "${workspace_root}" \
  --output "${workspace_root}/artifacts/firmware-sbom.json"

echo "[hesia] cloaked release build complete"
