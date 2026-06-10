#!/usr/bin/env bash
set -euo pipefail

ARDUPILOT_DIR="${ARDUPILOT_DIR:-/workspace/pixhawk/firmware/ardupilot}"
ARDUCOPTER_BIN="${ARDUCOPTER_BIN:-${ARDUPILOT_DIR}/build/sitl/bin/arducopter}"
ARDUPILOT_HOME="${ARDUPILOT_HOME:-48.8566,2.3522,120,0}"
SITL_MODEL="${SITL_MODEL:-quad}"
SITL_SPEEDUP="${SITL_SPEEDUP:-1}"
SITL_DEFAULTS="${SITL_DEFAULTS:-${ARDUPILOT_DIR}/Tools/autotest/default_params/copter.parm}"
SITL_BASE_PORT="${SITL_BASE_PORT:-5760}"

if [[ ! -x "${ARDUCOPTER_BIN}" ]]; then
  echo "ArduCopter SITL binary not executable: ${ARDUCOPTER_BIN}" >&2
  exit 2
fi

cd "${ARDUPILOT_DIR}"
exec "${ARDUCOPTER_BIN}" \
  --model "${SITL_MODEL}" \
  --speedup "${SITL_SPEEDUP}" \
  --home "${ARDUPILOT_HOME}" \
  --defaults "${SITL_DEFAULTS}" \
  --base-port "${SITL_BASE_PORT}"
