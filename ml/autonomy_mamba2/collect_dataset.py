"""Build JSONL training records from simulator frames and Pixhawk telemetry."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            records.append(json.loads(line))
    return records


def list_images(path: Path) -> list[Path]:
    return sorted(p for p in path.rglob("*") if p.suffix.lower() in IMAGE_EXTS)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image-dir", type=Path, required=True)
    parser.add_argument("--pixhawk-jsonl", type=Path, required=True)
    parser.add_argument("--scenario-manifest", type=Path, required=True)
    parser.add_argument("--yolo-summary-jsonl", type=Path)
    parser.add_argument("--out", type=Path, default=Path("F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba/train.jsonl"))
    args = parser.parse_args()

    images = list_images(args.image_dir)
    telemetry = load_jsonl(args.pixhawk_jsonl)
    manifest = json.loads(args.scenario_manifest.read_text(encoding="utf-8"))
    yolo_records = load_jsonl(args.yolo_summary_jsonl) if args.yolo_summary_jsonl and args.yolo_summary_jsonl.exists() else []
    scenarios = manifest.get("scenarios") or []
    count = min(len(images), len(telemetry))
    if count == 0:
        raise SystemExit("no paired image/telemetry samples available")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as fh:
        for idx in range(count):
            scenario = scenarios[idx % len(scenarios)] if scenarios else {}
            yolo = (yolo_records[idx].get("summary") if idx < len(yolo_records) else {}) or {}
            record = {
                "schema": "hesia.autonomy.sample.v1",
                "sample_id": f"sample_{idx:08d}",
                "frame_path": str(images[idx]),
                "telemetry": telemetry[idx],
                "yolo": yolo,
                "goal": scenario.get("target", {}),
                "disturbance": scenario.get("disturbance", {}),
                "expert_command": {
                    "north_mps": 0.0,
                    "east_mps": 0.0,
                    "down_mps_ned": 0.0,
                    "yaw_rate_rad_s": 0.0,
                    "source": "missing_expert_placeholder",
                },
                "scenario_id": scenario.get("scenario_id"),
            }
            fh.write(json.dumps(record, sort_keys=True) + "\n")

    print(json.dumps({"out": str(args.out), "samples": count, "images": len(images), "telemetry": len(telemetry)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
