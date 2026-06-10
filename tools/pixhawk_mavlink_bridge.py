#!/usr/bin/env python3
"""Bridge Pixhawk/ArduPilot MAVLink telemetry into a bounded HESIA JSON state."""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time
from pathlib import Path
from typing import Any


DEFAULT_MAVLINK_ROOTS = [
    os.environ.get("PIXHAWK_PYMAVLINK_ROOT", ""),
    "/workspace/pixhawk/firmware/ardupilot/modules/mavlink",
    "/mnt/f/Jetson-Ajax/pixhawk/firmware/ardupilot/modules/mavlink",
]


def add_pymavlink_path() -> None:
    for root in DEFAULT_MAVLINK_ROOTS:
        if root and Path(root).exists():
            sys.path.insert(0, root)
            return


add_pymavlink_path()
from pymavlink import mavutil  # type: ignore  # noqa: E402


def finite_float(value: Any, default: float | None = None) -> float | None:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def clamp(value: float | None, low: float, high: float) -> float | None:
    if value is None:
        return None
    return min(max(value, low), high)


def mavlink_temperature_c(raw_value: Any) -> float | None:
    raw = finite_float(raw_value)
    if raw is None or raw >= 32767 or raw <= -32768:
        return None
    return clamp(raw / 100.0, -80.0, 150.0)


def now_ms() -> int:
    return int(time.time() * 1000)


def empty_state(endpoint: str) -> dict[str, Any]:
    return {
        "schema": "hesia.pixhawk.state.v1",
        "source": "mavlink",
        "endpoint": endpoint,
        "received_at_ms": now_ms(),
        "heartbeat": {},
        "position": {},
        "attitude": {},
        "battery": {},
        "gps": {},
        "velocity": {},
    }


def update_state(state: dict[str, Any], msg: Any) -> bool:
    msg_type = msg.get_type()
    d = msg.to_dict()
    state["received_at_ms"] = now_ms()

    if msg_type == "HEARTBEAT":
        state["heartbeat"] = {
            "type": int(d.get("type", 0)),
            "autopilot": int(d.get("autopilot", 0)),
            "base_mode": int(d.get("base_mode", 0)),
            "custom_mode": int(d.get("custom_mode", 0)),
            "system_status": int(d.get("system_status", 0)),
            "mavlink_version": int(d.get("mavlink_version", 0)),
        }
        return True

    if msg_type == "GLOBAL_POSITION_INT":
        lat = clamp(finite_float(d.get("lat"), 0.0) / 1e7, -90.0, 90.0)
        lon = clamp(finite_float(d.get("lon"), 0.0) / 1e7, -180.0, 180.0)
        alt_m = clamp(finite_float(d.get("alt"), 0.0) / 1000.0, -1000.0, 100000.0)
        rel_alt_m = clamp(finite_float(d.get("relative_alt"), 0.0) / 1000.0, -1000.0, 100000.0)
        state["position"] = {
            "latitude_deg": lat,
            "longitude_deg": lon,
            "altitude_m": alt_m,
            "relative_altitude_m": rel_alt_m,
            "heading_deg": clamp(finite_float(d.get("hdg"), 0.0) / 100.0, 0.0, 360.0),
        }
        state["velocity"] = {
            "vx_mps": clamp(finite_float(d.get("vx"), 0.0) / 100.0, -200.0, 200.0),
            "vy_mps": clamp(finite_float(d.get("vy"), 0.0) / 100.0, -200.0, 200.0),
            "vz_mps": clamp(finite_float(d.get("vz"), 0.0) / 100.0, -200.0, 200.0),
        }
        return True

    if msg_type == "ATTITUDE":
        state["attitude"] = {
            "roll_rad": clamp(finite_float(d.get("roll")), -math.pi, math.pi),
            "pitch_rad": clamp(finite_float(d.get("pitch")), -math.pi, math.pi),
            "yaw_rad": clamp(finite_float(d.get("yaw")), -math.pi, math.pi),
            "rollspeed_rad_s": clamp(finite_float(d.get("rollspeed")), -20.0, 20.0),
            "pitchspeed_rad_s": clamp(finite_float(d.get("pitchspeed")), -20.0, 20.0),
            "yawspeed_rad_s": clamp(finite_float(d.get("yawspeed")), -20.0, 20.0),
        }
        return True

    if msg_type == "SYS_STATUS":
        state["battery"].update(
            {
                "voltage_v": clamp(finite_float(d.get("voltage_battery"), 0.0) / 1000.0, 0.0, 100.0),
                "current_a": clamp(finite_float(d.get("current_battery"), 0.0) / 100.0, -1.0, 1000.0),
                "remaining_pct": clamp(finite_float(d.get("battery_remaining")), -1.0, 100.0),
            }
        )
        return True

    if msg_type == "BATTERY_STATUS":
        state["battery"].update(
            {
                "battery_id": int(d.get("id", 0)),
                "temperature_c": mavlink_temperature_c(d.get("temperature")),
                "remaining_pct": clamp(finite_float(d.get("battery_remaining")), -1.0, 100.0),
            }
        )
        return True

    if msg_type == "GPS_RAW_INT":
        state["gps"] = {
            "fix_type": int(d.get("fix_type", 0)),
            "satellites_visible": int(d.get("satellites_visible", 0)),
            "eph": int(d.get("eph", 0)),
            "epv": int(d.get("epv", 0)),
        }
        return True

    if msg_type == "VFR_HUD":
        state["velocity"].update(
            {
                "airspeed_mps": clamp(finite_float(d.get("airspeed")), 0.0, 200.0),
                "groundspeed_mps": clamp(finite_float(d.get("groundspeed")), 0.0, 200.0),
                "climb_mps": clamp(finite_float(d.get("climb")), -100.0, 100.0),
            }
        )
        state["position"].update({"altitude_m": clamp(finite_float(d.get("alt")), -1000.0, 100000.0)})
        return True

    return False


def connect(endpoint: str, timeout_sec: float):
    deadline = time.time() + timeout_sec
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            return mavutil.mavlink_connection(endpoint, autoreconnect=True, source_system=255)
        except Exception as exc:  # pragma: no cover - depends on endpoint timing
            last_error = exc
            time.sleep(0.5)
    raise RuntimeError(f"unable to connect to MAVLink endpoint {endpoint}: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default=os.environ.get("PIXHAWK_MAVLINK_ENDPOINT", "tcp:127.0.0.1:5760"))
    parser.add_argument("--duration-sec", type=float, default=12.0)
    parser.add_argument("--connect-timeout-sec", type=float, default=20.0)
    parser.add_argument("--rate-hz", type=int, default=5)
    parser.add_argument("--min-updates", type=int, default=8)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.summary.parent.mkdir(parents=True, exist_ok=True)

    mav = connect(args.endpoint, args.connect_timeout_sec)
    state = empty_state(args.endpoint)
    counts: dict[str, int] = {}
    snapshots = 0
    first_state: dict[str, Any] | None = None

    start = time.time()
    next_gcs_heartbeat = 0.0
    heartbeat_seen = False

    with args.output.open("w", encoding="utf-8") as out:
        while time.time() - start < args.duration_sec:
            if time.time() >= next_gcs_heartbeat:
                mav.mav.heartbeat_send(
                    mavutil.mavlink.MAV_TYPE_GCS,
                    mavutil.mavlink.MAV_AUTOPILOT_INVALID,
                    0,
                    0,
                    mavutil.mavlink.MAV_STATE_ACTIVE,
                )
                next_gcs_heartbeat = time.time() + 1.0

            msg = mav.recv_match(blocking=True, timeout=0.5)
            if msg is None:
                continue
            msg_type = msg.get_type()
            if msg_type == "BAD_DATA":
                continue
            counts[msg_type] = counts.get(msg_type, 0) + 1

            if msg_type == "HEARTBEAT" and not heartbeat_seen:
                heartbeat_seen = True
                try:
                    mav.mav.request_data_stream_send(
                        mav.target_system,
                        mav.target_component,
                        mavutil.mavlink.MAV_DATA_STREAM_ALL,
                        args.rate_hz,
                        1,
                    )
                except Exception:
                    pass

            if update_state(state, msg):
                snapshots += 1
                snapshot = json.loads(json.dumps(state, sort_keys=True))
                if first_state is None:
                    first_state = snapshot
                out.write(json.dumps(snapshot, sort_keys=True) + "\n")
                out.flush()

    passed = heartbeat_seen and snapshots >= args.min_updates
    summary = {
        "schema": "hesia.pixhawk.bridge.summary.v1",
        "status": "passed" if passed else "failed",
        "endpoint": args.endpoint,
        "duration_sec": args.duration_sec,
        "heartbeat_seen": heartbeat_seen,
        "snapshot_count": snapshots,
        "message_counts": dict(sorted(counts.items())),
        "first_state": first_state,
        "last_state": state,
        "output": str(args.output),
    }
    args.summary.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
