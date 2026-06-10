#!/usr/bin/env python3
"""Bridge PLFM_RADAR replay data into bounded HESIA radar telemetry JSONL."""

from __future__ import annotations

import argparse
import csv
import json
import math
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "hesia.radar.frame.v1"


@dataclass(frozen=True)
class Detection:
    detection_id: int
    range_m: float
    azimuth_deg: float
    elevation_deg: float
    doppler_mps: float
    snr_db: float
    confidence: float
    source_bin: int


def finite_float(value: Any, name: str, lo: float, hi: float) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{name} is not numeric") from exc
    if not math.isfinite(parsed):
        raise ValueError(f"{name} is not finite")
    if parsed < lo or parsed > hi:
        raise ValueError(f"{name} out of range [{lo}, {hi}]")
    return parsed


def finite_int(value: Any, name: str, lo: int, hi: int) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{name} is not integer") from exc
    if parsed < lo or parsed > hi:
        raise ValueError(f"{name} out of range [{lo}, {hi}]")
    return parsed


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def validate_detection(det: Detection) -> None:
    finite_float(det.range_m, "range_m", 0.0, 30_000.0)
    finite_float(det.azimuth_deg, "azimuth_deg", -180.0, 180.0)
    finite_float(det.elevation_deg, "elevation_deg", -90.0, 90.0)
    finite_float(det.doppler_mps, "doppler_mps", -500.0, 500.0)
    finite_float(det.snr_db, "snr_db", -120.0, 120.0)
    finite_float(det.confidence, "confidence", 0.0, 1.0)
    finite_int(det.source_bin, "source_bin", 0, 4095)


def build_frame(frame_id: int, detections: list[Detection], input_path: Path, rejected_rows: int, total_rows: int) -> dict[str, Any]:
    for det in detections:
        validate_detection(det)
    return {
        "schema": SCHEMA,
        "frame_id": frame_id,
        "timestamp_ns": time.time_ns(),
        "source": {
            "system": "PLFM_RADAR",
            "input": str(input_path),
            "mode": "replay",
        },
        "detections": [asdict(det) for det in detections],
        "health": {
            "status": "ok",
            "total_rows": total_rows,
            "rejected_rows": rejected_rows,
            "detection_count": len(detections),
            "calibration": "range from sample_index; azimuth/elevation defaulted to 0 for CSV replay without angle metadata",
        },
    }


def csv_rows(path: Path) -> Iterable[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            yield row


def detections_from_csv(
    path: Path,
    threshold: float,
    max_detections: int,
    range_resolution_m: float,
    velocity_resolution_mps: float,
) -> tuple[list[Detection], int, int]:
    candidates: list[tuple[float, Detection]] = []
    rejected = 0
    total = 0

    for row in csv_rows(path):
        total += 1
        try:
            sample_index = finite_int(row.get("sample_index"), "sample_index", 0, 4095)
            chirp_number = finite_int(row.get("chirp_number"), "chirp_number", 0, 1_000_000)
            mag2 = finite_float(row.get("magnitude_squared"), "magnitude_squared", 0.0, 1.0e18)
        except ValueError:
            rejected += 1
            continue

        if mag2 < threshold:
            continue

        range_bin = sample_index % 64
        doppler_bin = chirp_number % 32
        centered_doppler = doppler_bin - 16
        snr_db = 10.0 * math.log10(max(mag2, 1.0))
        confidence = clamp01((snr_db - 10.0) / 35.0)
        det = Detection(
            detection_id=len(candidates),
            range_m=range_bin * range_resolution_m,
            azimuth_deg=0.0,
            elevation_deg=0.0,
            doppler_mps=centered_doppler * velocity_resolution_mps,
            snr_db=snr_db,
            confidence=confidence,
            source_bin=sample_index,
        )
        candidates.append((mag2, det))

    candidates.sort(key=lambda item: item[0], reverse=True)
    detections = [item[1] for item in candidates[:max_detections]]
    detections = [
        Detection(
            detection_id=idx,
            range_m=det.range_m,
            azimuth_deg=det.azimuth_deg,
            elevation_deg=det.elevation_deg,
            doppler_mps=det.doppler_mps,
            snr_db=det.snr_db,
            confidence=det.confidence,
            source_bin=det.source_bin,
        )
        for idx, det in enumerate(detections)
    ]
    return detections, rejected, total


def convert(args: argparse.Namespace) -> dict[str, Any]:
    input_path = Path(args.input)
    output_path = Path(args.output)
    summary_path = Path(args.summary)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    detections, rejected, total = detections_from_csv(
        input_path,
        threshold=args.threshold,
        max_detections=args.max_detections,
        range_resolution_m=args.range_resolution_m,
        velocity_resolution_mps=args.velocity_resolution_mps,
    )
    frame = build_frame(0, detections, input_path, rejected, total)
    with output_path.open("w", encoding="utf-8") as handle:
        handle.write(json.dumps(frame, sort_keys=True) + "\n")

    summary = {
        "schema": "hesia.radar.bridge.summary.v1",
        "input": str(input_path),
        "output": str(output_path),
        "summary": frame["health"],
        "top_detection": frame["detections"][0] if frame["detections"] else None,
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--threshold", type=float, default=100.0)
    parser.add_argument("--max-detections", type=int, default=32)
    parser.add_argument("--range-resolution-m", type=float, default=24.0)
    parser.add_argument("--velocity-resolution-mps", type=float, default=1.0)
    args = parser.parse_args()

    summary = convert(args)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
