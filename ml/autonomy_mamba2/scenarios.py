"""Scenario manifest generator for constrained autonomy dataset collection."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


SCENARIO_TEMPLATES = [
    ("calm_waypoint", {"calm": True}, 1.0),
    ("light_wind_waypoint", {"wind": True}, 0.9),
    ("crosswind_approach", {"wind": True, "crosswind": True}, 0.8),
    ("gust_recovery", {"wind": True, "gust": True}, 0.7),
    ("stall_recovery", {"stall": True}, 0.55),
    ("single_motor_loss", {"motor_loss": True}, 0.45),
    ("gps_noise_hold", {"gps_noise": True}, 0.65),
    ("low_battery_return", {"low_battery": True}, 0.75),
    ("gust_motor_loss", {"gust": True, "motor_loss": True}, 0.35),
    ("crosswind_stall", {"crosswind": True, "stall": True}, 0.4),
]


def build_manifest(repeats: int = 24) -> dict[str, object]:
    scenarios = []
    idx = 0
    for repeat in range(repeats):
        for name, flags, severity in SCENARIO_TEMPLATES:
            scenarios.append(
                {
                    "scenario_id": f"{name}_{repeat:03d}",
                    "name": name,
                    "disturbance": {"calm": False, "wind": False, "gust": False, "crosswind": False, "stall": False, "motor_loss": False, "gps_noise": False, "low_battery": False, **flags},
                    "severity": severity,
                    "duration_sec": 60 + int(severity * 40),
                    "target": {
                        "latitude_deg": 48.8566 + (idx % 7) * 0.00012,
                        "longitude_deg": 2.3522 + (idx % 11) * 0.00012,
                        "altitude_m": 115.0 + (idx % 5) * 5.0,
                    },
                    "required_outputs": ["rgb_frame", "yolo11m_seg_summary", "pixhawk_state", "expert_command"],
                }
            )
            idx += 1
    return {
        "schema": "hesia.autonomy.scenario_manifest.v1",
        "scenario_count": len(scenarios),
        "templates": len(SCENARIO_TEMPLATES),
        "scenarios": scenarios,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=Path("F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba/scenario_manifest.json"))
    parser.add_argument("--repeats", type=int, default=24)
    args = parser.parse_args()
    payload = build_manifest(args.repeats)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps({"out": str(args.out), "scenario_count": payload["scenario_count"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
