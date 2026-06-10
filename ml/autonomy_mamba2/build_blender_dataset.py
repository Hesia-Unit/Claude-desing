"""Build train/val/test JSONL records from Blender frames and Pixhawk telemetry."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


EARTH_M_PER_DEG_LAT = 111_320.0


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def write_jsonl(path: Path, records: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        for record in records:
            fh.write(json.dumps(record, sort_keys=True) + "\n")


def telemetry_position(record: dict[str, Any]) -> tuple[float, float, float]:
    pos = record.get("position") or {}
    return (
        float(pos.get("latitude_deg", 48.8566)),
        float(pos.get("longitude_deg", 2.3522)),
        float(pos.get("altitude_m", 120.0)),
    )


def goal_delta(telemetry: dict[str, Any], target: dict[str, Any], mission_step: int, step_progress: float) -> dict[str, float]:
    lat, lon, alt = telemetry_position(telemetry)
    target_lat = float(target.get("latitude_deg", lat))
    target_lon = float(target.get("longitude_deg", lon))
    target_alt = float(target.get("altitude_m", alt))
    north = (target_lat - lat) * EARTH_M_PER_DEG_LAT
    east = (target_lon - lon) * EARTH_M_PER_DEG_LAT * math.cos(math.radians(lat))
    delta_alt = target_alt - alt
    distance = math.sqrt(north * north + east * east)
    bearing = math.degrees(math.atan2(east, north)) % 360.0 if distance > 1e-6 else 0.0
    return {
        "delta_north_m": north,
        "delta_east_m": east,
        "delta_alt_m": delta_alt,
        "distance_m": distance,
        "bearing_deg": bearing,
        "target_altitude_m": target_alt,
        "mission_step": float(mission_step),
        "step_progress": step_progress,
    }


def clamp(value: float, limit: float = 1.0) -> float:
    return max(-limit, min(limit, value))


def expert_command(goal: dict[str, float], perception: dict[str, Any], disturbance: dict[str, Any]) -> dict[str, Any]:
    obstacle = float(perception.get("obstacle_fraction", 0.0))
    safe = float(perception.get("safe_surface_fraction", 1.0))
    north = clamp(goal["delta_north_m"] / 35.0)
    east = clamp(goal["delta_east_m"] / 35.0)
    down = clamp(-goal["delta_alt_m"] / 8.0)
    yaw = clamp(((goal["bearing_deg"] + 180.0) % 360.0 - 180.0) / 90.0)
    mode = "cruise"

    if obstacle > 0.28 or safe < 0.52:
        mode = "avoid"
        north *= 0.45
        east = clamp(east + (0.35 if perception.get("left_obstacle_fraction", 0.0) > perception.get("right_obstacle_fraction", 0.0) else -0.35))
        down = min(down, -0.25)
    if disturbance.get("stall"):
        mode = "recover_stall"
        north = 0.15
        east *= 0.2
        down = -0.85
        yaw *= 0.25
    elif disturbance.get("motor_loss"):
        mode = "motor_loss_recovery"
        north *= 0.35
        east *= 0.35
        down = min(down, -0.25)
        yaw *= 0.45
    elif disturbance.get("low_battery"):
        mode = "return_home"
        north *= 0.6
        east *= 0.6
        down = max(down, 0.05)
    elif abs(goal["delta_alt_m"]) > 5.0:
        mode = "climb" if goal["delta_alt_m"] > 0 else "descend"

    if disturbance.get("crosswind"):
        east = clamp(east - 0.18)
    if disturbance.get("gust"):
        north = clamp(north * 0.75)
        east = clamp(east * 0.75)

    return {
        "north_mps": north,
        "east_mps": east,
        "down_mps_ned": down,
        "yaw_rate_rad_s": yaw,
        "mode": mode,
        "source": "blender_pixhawk_rule_expert_v1",
    }


def merge_perception(render_meta: dict[str, Any], yolo_summary: dict[str, Any] | None) -> dict[str, Any]:
    semantic = (render_meta.get("semantic") or {}).copy()
    yolo = (yolo_summary or {}).copy()
    coverage = {}
    coverage.update(semantic.get("mask_coverage") or {})
    coverage.update(yolo.get("mask_coverage") or {})
    semantic["mask_coverage"] = coverage
    semantic["instances"] = int(yolo.get("instances", 0))
    semantic["mean_confidence"] = float(yolo.get("mean_confidence", 0.0))
    semantic["source"] = "yolo11m_seg_plus_blender_semantics"
    return semantic


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--render-metadata", type=Path, required=True)
    parser.add_argument("--pixhawk-jsonl", type=Path, required=True)
    parser.add_argument("--scenario-manifest", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, default=Path("F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba"))
    parser.add_argument("--max-samples", type=int, default=0)
    args = parser.parse_args()

    render_records = load_jsonl(args.render_metadata)
    telemetry = load_jsonl(args.pixhawk_jsonl)
    manifest = json.loads(args.scenario_manifest.read_text(encoding="utf-8"))
    scenarios = {item["scenario_id"]: item for item in manifest.get("scenarios", [])}
    count = min(len(render_records), len(telemetry))
    if args.max_samples > 0:
        count = min(count, args.max_samples)
    if count < 16:
        raise SystemExit(f"not enough paired samples: {count}")

    records = []
    for idx in range(count):
        render = render_records[idx]
        tel = telemetry[idx % len(telemetry)]
        scenario = scenarios.get(render.get("scenario_id"), {})
        target = scenario.get("target") or {}
        disturbance = render.get("disturbance") or scenario.get("disturbance") or {}
        perception = merge_perception(render, None)
        goal = goal_delta(tel, target, idx % 20, float((render.get("semantic") or {}).get("step_progress", 0.0)))
        records.append(
            {
                "schema": "hesia.autonomy.sample.v1",
                "sample_id": f"blender_pixhawk_{idx:08d}",
                "frame_path": render.get("frame_path"),
                "telemetry": tel,
                "yolo": perception,
                "goal": goal,
                "disturbance": disturbance,
                "expert_command": expert_command(goal, perception, disturbance),
                "scenario_id": render.get("scenario_id"),
                "simulator": "blender_5_1_procedural_visual",
            }
        )

    train_end = max(16, int(len(records) * 0.7))
    val_end = max(train_end + 1, int(len(records) * 0.85))
    train = records[:train_end]
    val = records[train_end:val_end]
    test = records[val_end:]
    write_jsonl(args.out_dir / "train.jsonl", train)
    write_jsonl(args.out_dir / "val.jsonl", val)
    write_jsonl(args.out_dir / "test.jsonl", test)
    summary = {
        "schema": "hesia.autonomy.blender_dataset.v1",
        "status": "passed",
        "samples": len(records),
        "train": len(train),
        "val": len(val),
        "test": len(test),
        "placeholder_commands": sum(1 for item in records if item["expert_command"]["source"] == "missing_expert_placeholder"),
        "out_dir": str(args.out_dir),
        "render_metadata": str(args.render_metadata),
        "pixhawk_jsonl": str(args.pixhawk_jsonl),
    }
    (args.out_dir / "dataset_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
