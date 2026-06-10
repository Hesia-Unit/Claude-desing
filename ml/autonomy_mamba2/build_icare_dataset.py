"""Build the enriched F: dataset used by Icare.

The source dataset already contains simulator frames, Pixhawk snapshots and
rule-expert velocity commands.  This converter adds mission text, MiDaS-style
depth summaries, radar detections and continuous actuator labels.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def write_jsonl(path: Path, records: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, sort_keys=True, ensure_ascii=True) + "\n")


def clamp(value: float, lo: float = -1.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, value))


def flag_text(disturbance: dict[str, Any]) -> str:
    active = [key for key, value in disturbance.items() if value and key != "calm"]
    return ", ".join(active) if active else "conditions calmes"


def mission_text(record: dict[str, Any]) -> str:
    goal = record.get("goal") or {}
    disturbance = record.get("disturbance") or {}
    distance = float(goal.get("distance_m", 0.0))
    delta_alt = float(goal.get("delta_alt_m", 0.0))
    distance_band = "proche" if distance < 20.0 else "intermediaire" if distance < 120.0 else "lointain"
    altitude_band = "monter" if delta_alt > 3.0 else "descendre" if delta_alt < -3.0 else "maintenir altitude"
    return (
        "Mission UAV: rejoindre le waypoint cible, maintenir une trajectoire stable, "
        "eviter les obstacles detectes par vision et radar, compenser les perturbations "
        f"({flag_text(disturbance)}), puis considerer la mission terminee quand la distance "
        f"au waypoint est faible. Distance {distance_band}; consigne altitude: {altitude_band}."
    )


def synthetic_midas(record: dict[str, Any]) -> dict[str, float]:
    yolo = record.get("yolo") or {}
    center = float(yolo.get("center_obstacle_fraction", 0.0))
    left = float(yolo.get("left_obstacle_fraction", 0.0))
    right = float(yolo.get("right_obstacle_fraction", 0.0))
    obstacle = float(yolo.get("obstacle_fraction", 0.0))
    horizon = float(yolo.get("horizon_clearance", 1.0))
    return {
        "schema": "hesia.midas.summary.v1",
        "source": "synthetic_from_blender_semantics_or_existing_midas_contract",
        "depth_mean": clamp(0.75 - obstacle * 0.45, 0.0, 1.0),
        "depth_min": clamp(0.9 - max(center, left, right) * 2.2, 0.0, 1.0),
        "center_depth_min": clamp(0.92 - center * 2.4, 0.0, 1.0),
        "left_depth_min": clamp(0.92 - left * 2.2, 0.0, 1.0),
        "right_depth_min": clamp(0.92 - right * 2.2, 0.0, 1.0),
        "horizon_clearance": clamp(horizon, 0.0, 1.0),
        "vertical_gradient": clamp(horizon - obstacle, -1.0, 1.0),
        "asymmetry": clamp(left - right, -1.0, 1.0),
    }


def synthetic_radar(record: dict[str, Any]) -> dict[str, Any]:
    yolo = record.get("yolo") or {}
    disturbance = record.get("disturbance") or {}
    obstacle = float(yolo.get("obstacle_fraction", 0.0))
    center = float(yolo.get("center_obstacle_fraction", 0.0))
    left = float(yolo.get("left_obstacle_fraction", 0.0))
    right = float(yolo.get("right_obstacle_fraction", 0.0))
    detections = []
    candidates = [
        (center, 0.0),
        (left, -45.0),
        (right, 45.0),
    ]
    for idx, (strength, az) in enumerate(candidates):
        if strength <= 0.03 and not disturbance.get("gps_noise"):
            continue
        range_m = 1800.0 - min(strength, 0.6) * 1500.0
        if disturbance.get("stall"):
            range_m *= 0.75
        doppler = -8.0 if strength > 0.12 else -2.0
        detections.append(
            {
                "detection_id": idx,
                "range_m": max(80.0, range_m),
                "azimuth_deg": az,
                "elevation_deg": 0.0,
                "doppler_mps": doppler,
                "snr_db": 16.0 + strength * 45.0,
                "confidence": clamp(0.25 + strength * 2.0, 0.0, 1.0),
                "source_bin": idx,
            }
        )
    if obstacle < 0.04 and not detections:
        detections.append(
            {
                "detection_id": 0,
                "range_m": 2800.0,
                "azimuth_deg": 0.0,
                "elevation_deg": 0.0,
                "doppler_mps": 0.0,
                "snr_db": 8.0,
                "confidence": 0.05,
                "source_bin": 0,
            }
        )
    return {
        "schema": "hesia.radar.frame.v1",
        "source": {"system": "PLFM_RADAR", "mode": "synthetic_training_bridge"},
        "detections": detections,
        "health": {"status": "ok", "detection_count": len(detections)},
    }


def synthetic_tracks(record: dict[str, Any]) -> dict[str, Any]:
    yolo = record.get("yolo") or {}
    tracks = []
    areas = [
        ("center_obstacle", float(yolo.get("center_obstacle_fraction", 0.0)), 0.5, 0.58),
        ("left_obstacle", float(yolo.get("left_obstacle_fraction", 0.0)), 0.25, 0.56),
        ("right_obstacle", float(yolo.get("right_obstacle_fraction", 0.0)), 0.75, 0.56),
    ]
    for idx, (label, area, cx, cy) in enumerate(areas):
        if area <= 0.025:
            continue
        tracks.append(
            {
                "track_id": idx + 1,
                "class_name": label,
                "confidence": clamp(0.45 + area * 1.8, 0.0, 1.0),
                "bbox_area_fraction": area,
                "mask_area_fraction": area,
                "center": {"x": cx, "y": cy},
                "age": 3,
                "last_seen": 0,
            }
        )
    return {"schema": "hesia.yolo11.track_summary.v1", "source": "synthetic_from_semantic_summary", "tracks": tracks}


def actuator_labels(command: dict[str, Any], disturbance: dict[str, Any], goal: dict[str, Any]) -> dict[str, Any]:
    north = float(command.get("north_mps", 0.0))
    east = float(command.get("east_mps", 0.0))
    down = float(command.get("down_mps_ned", 0.0))
    yaw = float(command.get("yaw_rate_rad_s", 0.0))
    speed = min(1.0, math.sqrt(north * north + east * east + down * down))
    throttle = clamp(0.38 + 0.42 * speed + (0.12 if command.get("mode") in {"climb", "recover_stall"} else 0.0), 0.0, 1.0)
    motors = [throttle, throttle, throttle, throttle]
    if disturbance.get("motor_loss"):
        motors[1] = 0.0
        motors[0] = clamp(motors[0] + 0.12, 0.0, 1.0)
        motors[2] = clamp(motors[2] + 0.08, 0.0, 1.0)
    if command.get("mode") == "mission_complete":
        motors = [0.18, 0.18, 0.18, 0.18]

    elevator = clamp(-down * 0.7)
    aileron = clamp(east * 0.55)
    rudder = clamp(yaw * 0.65)
    flap = 0.0
    if command.get("mode") in {"climb", "recover_stall"}:
        flap = 0.35
    elif command.get("mode") == "descend":
        flap = 0.18
    elif command.get("mode") == "mission_complete":
        flap = 0.55
    return {
        "motors": motors,
        "servos": {
            "servo_aileron_left": -aileron,
            "servo_aileron_right": aileron,
            "servo_elevator_left": elevator,
            "servo_elevator_right": elevator,
            "servo_rudder_left": -rudder,
            "servo_rudder_right": rudder,
            "servo_flap_left": flap,
            "servo_flap_right": flap,
            "servo_airbrake": 0.35 if command.get("mode") in {"return_home", "mission_complete"} else 0.0,
            "servo_landing_gear": 1.0 if command.get("mode") == "mission_complete" else 0.0,
        },
    }


def enrich_record(record: dict[str, Any]) -> dict[str, Any]:
    out = dict(record)
    out["schema"] = "hesia.icare.sample.v1"
    out["mission_text"] = mission_text(record)
    out["midas"] = synthetic_midas(record)
    out["radar"] = synthetic_radar(record)
    out["yolo_tracks"] = synthetic_tracks(record)
    command = dict(record.get("expert_command") or {})
    goal = record.get("goal") or {}
    progress = float(goal.get("step_progress", 0.0))
    distance = float(goal.get("distance_m", 999.0))
    complete = progress >= 0.985 or (distance <= 1.5 and progress > 0.7)
    if complete:
        command.update(
            {
                "north_mps": 0.0,
                "east_mps": 0.0,
                "down_mps_ned": 0.0,
                "yaw_rate_rad_s": 0.0,
                "mode": "mission_complete",
            }
        )
    command["actuators"] = actuator_labels(command, record.get("disturbance") or {}, goal)
    command["mission_complete"] = bool(complete)
    command["source"] = command.get("source", "unknown") + "+icare_actuator_labels_v1"
    out["expert_command"] = command
    return out


def add_completion_samples(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_scenario: dict[str, dict[str, Any]] = {}
    for record in records:
        sid = str(record.get("scenario_id"))
        by_scenario[sid] = record
    additions = []
    for idx, record in enumerate(by_scenario.values()):
        clone = json.loads(json.dumps(record))
        clone["sample_id"] = f"{clone.get('sample_id', 'sample')}_complete"
        clone.setdefault("goal", {})
        clone["goal"].update({"distance_m": 0.0, "delta_north_m": 0.0, "delta_east_m": 0.0, "delta_alt_m": 0.0, "step_progress": 1.0})
        clone["mission_text"] = mission_text(clone) + " Etat final: waypoint atteint, stabiliser et cloturer la mission."
        clone["expert_command"] = {
            "north_mps": 0.0,
            "east_mps": 0.0,
            "down_mps_ned": 0.0,
            "yaw_rate_rad_s": 0.0,
            "mode": "mission_complete",
            "actuators": actuator_labels({"mode": "mission_complete"}, clone.get("disturbance") or {}, clone.get("goal") or {}),
            "mission_complete": True,
            "source": "icare_synthetic_completion_v1",
        }
        clone["sample_id"] = f"icare_completion_{idx:06d}"
        additions.append(clone)
    return records + additions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path("F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba"))
    parser.add_argument("--out-dir", type=Path, default=Path("F:/Set-Donner/datasets/icare_autonomy_v1"))
    args = parser.parse_args()

    summary = {"schema": "hesia.icare.dataset_summary.v1", "source_dir": str(args.source_dir), "out_dir": str(args.out_dir)}
    totals = {}
    for split in ("train", "val", "test"):
        source = args.source_dir / f"{split}.jsonl"
        records = [enrich_record(record) for record in load_jsonl(source)]
        if split == "train":
            records = add_completion_samples(records)
        write_jsonl(args.out_dir / f"{split}.jsonl", records)
        totals[split] = len(records)
        totals[f"{split}_mission_complete"] = sum(1 for item in records if (item.get("expert_command") or {}).get("mission_complete"))
    summary.update(totals)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "dataset_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
