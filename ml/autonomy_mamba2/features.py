"""Feature packing for YOLO11m-seg + Pixhawk + mission-goal autonomy records."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import numpy as np


FEATURE_DIM = 64


@dataclass(frozen=True, slots=True)
class FlightFeatureSpec:
    feature_dim: int = FEATURE_DIM
    yolo_slots: int = 16
    telemetry_slots: int = 20
    goal_slots: int = 8
    disturbance_slots: int = 8
    reserve_slots: int = 12


DISTURBANCE_KEYS = [
    "calm",
    "wind",
    "gust",
    "crosswind",
    "stall",
    "motor_loss",
    "gps_noise",
    "low_battery",
]


def _value(record: dict[str, Any], path: str, default: float = 0.0) -> float:
    cur: Any = record
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return default
        cur = cur[part]
    try:
        value = float(cur)
    except (TypeError, ValueError):
        return default
    return value if np.isfinite(value) else default


def pack_record(record: dict[str, Any]) -> np.ndarray:
    """Pack one dataset record into a bounded 64-float controller vector."""

    feat = np.zeros(FEATURE_DIM, dtype=np.float32)
    yolo = record.get("yolo") or {}
    telemetry = record.get("telemetry") or {}
    goal = record.get("goal") or {}
    disturbance = record.get("disturbance") or {}

    feat[0] = _value(yolo, "mask_coverage.person")
    feat[1] = _value(yolo, "mask_coverage.car")
    feat[2] = _value(yolo, "mask_coverage.truck")
    feat[3] = _value(yolo, "mask_coverage.tree")
    feat[4] = _value(yolo, "mask_coverage.road")
    feat[5] = _value(yolo, "mask_coverage.sky")
    feat[6] = _value(yolo, "obstacle_fraction")
    feat[7] = _value(yolo, "safe_surface_fraction")
    feat[8] = _value(yolo, "mean_confidence")
    feat[9] = min(_value(yolo, "instances") / 100.0, 1.0)
    feat[10] = _value(yolo, "center_obstacle_fraction")
    feat[11] = _value(yolo, "left_obstacle_fraction")
    feat[12] = _value(yolo, "right_obstacle_fraction")
    feat[13] = _value(yolo, "horizon_clearance")
    feat[14] = _value(yolo, "motion_blur_score")
    feat[15] = _value(yolo, "exposure_risk")

    telemetry_paths = [
        "position.latitude_deg",
        "position.longitude_deg",
        "position.altitude_m",
        "position.relative_altitude_m",
        "position.heading_deg",
        "attitude.roll_rad",
        "attitude.pitch_rad",
        "attitude.yaw_rad",
        "velocity.vx_mps",
        "velocity.vy_mps",
        "velocity.vz_mps",
        "velocity.groundspeed_mps",
        "velocity.airspeed_mps",
        "velocity.climb_mps",
        "battery.voltage_v",
        "battery.current_a",
        "battery.remaining_pct",
        "gps.fix_type",
        "gps.satellites_visible",
        "health.cpu_temp_c",
    ]
    for idx, path in enumerate(telemetry_paths, start=16):
        feat[idx] = _value(telemetry, path)

    goal_paths = [
        "delta_north_m",
        "delta_east_m",
        "delta_alt_m",
        "distance_m",
        "bearing_deg",
        "target_altitude_m",
        "mission_step",
        "step_progress",
    ]
    for idx, path in enumerate(goal_paths, start=36):
        feat[idx] = _value(goal, path)

    for idx, key in enumerate(DISTURBANCE_KEYS, start=44):
        feat[idx] = 1.0 if disturbance.get(key) else 0.0

    scales = np.ones_like(feat)
    scales[16:18] = 180.0
    scales[18:20] = 1000.0
    scales[20] = 360.0
    scales[21:24] = np.pi
    scales[24:30] = 50.0
    scales[30:32] = 30.0
    scales[32] = 100.0
    scales[33] = 10.0
    scales[34] = 20.0
    scales[35] = 150.0
    scales[36:40] = 500.0
    scales[40] = 360.0
    scales[41] = 500.0
    scales[42:44] = 20.0
    return np.clip(feat / scales, -5.0, 5.0)


def pack_sequence(records: list[dict[str, Any]]) -> np.ndarray:
    if not records:
        raise ValueError("records must not be empty")
    return np.stack([pack_record(record) for record in records], axis=0)
