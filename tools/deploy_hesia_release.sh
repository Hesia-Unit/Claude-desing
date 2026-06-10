#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <drone_bin> <server_bin> [drone_debug] [server_debug] [sentinel_lib]" >&2
  exit 2
fi

drone_bin="$1"
server_bin="$2"
drone_debug="${3:-}"
server_debug="${4:-}"
sentinel_lib="${5:-}"

release_root="${HESIA_RELEASE_ROOT:-/opt/hesia}"
release_bin_dir="${release_root}/bin"
release_lib_dir="${release_root}/lib"
release_debug_dir="${HESIA_RELEASE_DEBUG_DIR:-/var/lib/hesia/debug}"
immutable="${HESIA_DEPLOY_IMMUTABLE:-0}"
patchelf_bin="${HESIA_PATCHELF_BIN:-$(command -v patchelf || true)}"

install -d -m 0750 -o root -g hesia "${release_bin_dir}"
install -d -m 0750 -o root -g hesia "${release_lib_dir}"
install -d -m 0700 -o root -g root "${release_debug_dir}"

chattr -i "${release_bin_dir}/hesia_drone" 2>/dev/null || true
chattr -i "${release_bin_dir}/hesia_server_cpp" 2>/dev/null || true
chattr -i "${release_lib_dir}/libhesia_sentinel.so" 2>/dev/null || true

install -o root -g hesia -m 0750 "${drone_bin}" "${release_bin_dir}/hesia_drone"
install -o root -g hesia -m 0750 "${server_bin}" "${release_bin_dir}/hesia_server_cpp"

if [ -n "${sentinel_lib}" ] && [ -f "${sentinel_lib}" ]; then
  install -o root -g hesia -m 0750 "${sentinel_lib}" "${release_lib_dir}/libhesia_sentinel.so"
fi

if [ -n "${patchelf_bin}" ] && [ -x "${patchelf_bin}" ] && [ -f "${release_bin_dir}/hesia_drone" ]; then
  sentinel_needed="$("${patchelf_bin}" --print-needed "${release_bin_dir}/hesia_drone" | awk '/libhesia_sentinel\\.so$/ {print; exit}')"
  if [ -n "${sentinel_needed}" ] && [ "${sentinel_needed}" != "libhesia_sentinel.so" ]; then
    "${patchelf_bin}" --replace-needed "${sentinel_needed}" "libhesia_sentinel.so" "${release_bin_dir}/hesia_drone"
  fi
  "${patchelf_bin}" --set-rpath '$ORIGIN/../lib:/usr/local/cuda/lib64' "${release_bin_dir}/hesia_drone"
fi

if [ -n "${drone_debug}" ] && [ -f "${drone_debug}" ]; then
  install -o root -g root -m 0600 "${drone_debug}" "${release_debug_dir}/hesia_drone.debug"
fi

if [ -n "${server_debug}" ] && [ -f "${server_debug}" ]; then
  install -o root -g root -m 0600 "${server_debug}" "${release_debug_dir}/hesia_server_cpp.debug"
fi

if [ "${immutable}" = "1" ]; then
  chattr +i "${release_bin_dir}/hesia_drone" 2>/dev/null || true
  chattr +i "${release_bin_dir}/hesia_server_cpp" 2>/dev/null || true
  if [ -f "${release_lib_dir}/libhesia_sentinel.so" ]; then
    chattr +i "${release_lib_dir}/libhesia_sentinel.so" 2>/dev/null || true
  fi
fi

echo "[deploy] Release binaries installed in ${release_bin_dir}"
