#!/usr/bin/env python3
"""Bounded Icare command to MAVLink bridge.

Default mode is dry-run.  Sending to a live endpoint requires --send and keeps
the deterministic guardrail in place before any MAVLink message is produced.
"""

from __future__ import annotations

import argparse
import json
import time
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from ml.autonomy_mamba2.features_icare import bounded_command


SERVO_CHANNELS = {
    "servo_aileron_left": 1,
    "servo_aileron_right": 2,
    "servo_elevator_left": 3,
    "servo_elevator_right": 4,
    "servo_rudder_left": 5,
    "servo_rudder_right": 6,
    "servo_flap_left": 7,
    "servo_flap_right": 8,
    "servo_airbrake": 9,
    "servo_landing_gear": 10,
}


def pwm_from_servo(value: float) -> int:
    return int(round(1500 + max(-1.0, min(1.0, value)) * 500))


def motor_controls(motors: list[float]) -> list[float]:
    out = [max(0.0, min(1.0, float(v))) for v in motors[:4]]
    out += [0.0] * (4 - len(out))
    return out + [0.0] * 4


def mavlink_payload(command: dict[str, Any]) -> dict[str, Any]:
    bounded = bounded_command(command)
    actuators = bounded["actuators"]
    return {
        "schema": "hesia.icare.mavlink_payload.v1",
        "timestamp_ns": time.time_ns(),
        "mode": bounded.get("mode", "hold"),
        "velocity": bounded["velocity"],
        "set_actuator_control_target": {
            "group_mlx": 0,
            "controls": motor_controls(actuators.get("motors") or []),
        },
        "servo_pwm": {
            str(SERVO_CHANNELS[name]): pwm_from_servo(value)
            for name, value in (actuators.get("servos") or {}).items()
            if name in SERVO_CHANNELS
        },
        "guardrail": bounded["guardrail"],
    }


def send_payload(endpoint: str, payload: dict[str, Any]) -> None:
    from pymavlink import mavutil

    mav = mavutil.mavlink_connection(endpoint, autoreconnect=True, source_system=255)
    mav.wait_heartbeat(timeout=10)
    mav.mav.set_actuator_control_target_send(
        int(time.time() * 1_000_000),
        mav.target_system,
        mav.target_component,
        int(payload["set_actuator_control_target"]["group_mlx"]),
        payload["set_actuator_control_target"]["controls"],
    )
    for channel, pwm in payload["servo_pwm"].items():
        mav.mav.command_long_send(
            mav.target_system,
            mav.target_component,
            mavutil.mavlink.MAV_CMD_DO_SET_SERVO,
            0,
            int(channel),
            int(pwm),
            0,
            0,
            0,
            0,
            0,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--command-json", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("artifacts/icare/mavlink_payload.json"))
    parser.add_argument("--endpoint", default="tcp:127.0.0.1:5760")
    parser.add_argument("--send", action="store_true")
    args = parser.parse_args()

    command = json.loads(args.command_json.read_text(encoding="utf-8-sig"))
    payload = mavlink_payload(command)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    if args.send:
        send_payload(args.endpoint, payload)
    print(json.dumps({"status": "sent" if args.send else "dry_run", "out": str(args.out)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
