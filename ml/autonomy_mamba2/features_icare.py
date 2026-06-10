"""Feature and command packing for the Icare autonomy policy.

Icare keeps the existing Ajax perception pipeline outside this module.  This
file only defines the local C: training/inference contract consumed by the
decision model, with dataset records and logs stored on F:.
"""

from __future__ import annotations

from typing import Any

import numpy as np

from .features import FEATURE_DIM, pack_record


ICARE_FEATURE_DIM = 96
RADAR_DIM = 12
MIDAS_DIM = 8
TRACK_DIM = 12

ICARE_ACTION_NAMES = [
    "north_mps",
    "east_mps",
    "down_mps_ned",
    "yaw_rate_rad_s",
    "motor_0",
    "motor_1",
    "motor_2",
    "motor_3",
    "servo_aileron_left",
    "servo_aileron_right",
    "servo_elevator_left",
    "servo_elevator_right",
    "servo_rudder_left",
    "servo_rudder_right",
    "servo_flap_left",
    "servo_flap_right",
    "servo_airbrake",
    "servo_landing_gear",
]
ICARE_ACTION_DIM = len(ICARE_ACTION_NAMES)

ICARE_MODES = {
    "hold": 0,
    "cruise": 1,
    "climb": 2,
    "descend": 3,
    "avoid": 4,
    "recover_stall": 5,
    "motor_loss_recovery": 6,
    "return_home": 7,
    "mission_complete": 8,
    "emergency_land": 9,
}
ID_TO_ICARE_MODE = {value: key for key, value in ICARE_MODES.items()}


def _finite(value: Any, default: float = 0.0) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if np.isfinite(out) else default


def _clip(value: float, lo: float, hi: float) -> float:
    return float(max(lo, min(hi, value)))


def _norm_angle_deg(value: float) -> float:
    return (((value + 180.0) % 360.0) - 180.0) / 180.0


def pack_radar(record: dict[str, Any]) -> np.ndarray:
    radar = record.get("radar") or {}
    detections = radar.get("detections") or []
    feat = np.zeros(RADAR_DIM, dtype=np.float32)
    if not detections:
        return feat

    ranges = np.asarray([_finite(d.get("range_m"), 30_000.0) for d in detections], dtype=np.float32)
    az = np.asarray([_finite(d.get("azimuth_deg"), 0.0) for d in detections], dtype=np.float32)
    doppler = np.asarray([_finite(d.get("doppler_mps"), 0.0) for d in detections], dtype=np.float32)
    conf = np.asarray([_finite(d.get("confidence"), 0.0) for d in detections], dtype=np.float32)
    snr = np.asarray([_finite(d.get("snr_db"), 0.0) for d in detections], dtype=np.float32)
    closest = int(np.argmin(ranges))
    front = np.abs(az) < 25.0
    left = (az < -25.0) & (az > -115.0)
    right = (az > 25.0) & (az < 115.0)
    closing = doppler < -1.0

    feat[0] = min(len(detections) / 32.0, 1.0)
    feat[1] = _clip(float(ranges[closest]) / 30_000.0, 0.0, 1.0)
    feat[2] = _norm_angle_deg(float(az[closest]))
    feat[3] = _clip(float(doppler[closest]) / 100.0, -1.0, 1.0)
    feat[4] = _clip(float(conf.mean()), 0.0, 1.0)
    feat[5] = _clip(float(snr.mean()) / 80.0, -1.0, 1.0)
    feat[6] = _clip(float(conf[front].sum()) if front.any() else 0.0, 0.0, 1.0)
    feat[7] = _clip(float(conf[left].sum()) if left.any() else 0.0, 0.0, 1.0)
    feat[8] = _clip(float(conf[right].sum()) if right.any() else 0.0, 0.0, 1.0)
    feat[9] = _clip(float(conf[closing].sum()) if closing.any() else 0.0, 0.0, 1.0)
    feat[10] = _clip(float((conf / np.maximum(ranges, 1.0) * 500.0).sum()), 0.0, 1.0)
    feat[11] = 1.0 if radar.get("health", {}).get("status", "ok") == "ok" else -1.0
    return feat


def pack_midas(record: dict[str, Any]) -> np.ndarray:
    midas = record.get("midas") or {}
    feat = np.zeros(MIDAS_DIM, dtype=np.float32)
    feat[0] = _clip(_finite(midas.get("depth_mean"), 0.5), 0.0, 1.0)
    feat[1] = _clip(_finite(midas.get("depth_min"), 0.5), 0.0, 1.0)
    feat[2] = _clip(_finite(midas.get("center_depth_min"), 0.5), 0.0, 1.0)
    feat[3] = _clip(_finite(midas.get("left_depth_min"), 0.5), 0.0, 1.0)
    feat[4] = _clip(_finite(midas.get("right_depth_min"), 0.5), 0.0, 1.0)
    feat[5] = _clip(_finite(midas.get("horizon_clearance"), 1.0), 0.0, 1.0)
    feat[6] = _clip(_finite(midas.get("vertical_gradient"), 0.0), -1.0, 1.0)
    feat[7] = _clip(_finite(midas.get("asymmetry"), 0.0), -1.0, 1.0)
    return feat


def pack_tracks(record: dict[str, Any]) -> np.ndarray:
    tracks = (record.get("yolo_tracks") or {}).get("tracks") or []
    feat = np.zeros(TRACK_DIM, dtype=np.float32)
    if not tracks:
        return feat

    conf = np.asarray([_finite(t.get("confidence"), 0.0) for t in tracks], dtype=np.float32)
    area = np.asarray([_finite(t.get("mask_area_fraction"), _finite(t.get("bbox_area_fraction"), 0.0)) for t in tracks], dtype=np.float32)
    cx = np.asarray([_finite((t.get("center") or {}).get("x"), 0.5) for t in tracks], dtype=np.float32)
    cy = np.asarray([_finite((t.get("center") or {}).get("y"), 0.5) for t in tracks], dtype=np.float32)
    age = np.asarray([_finite(t.get("age"), 1.0) for t in tracks], dtype=np.float32)
    front = (cx > 0.35) & (cx < 0.65) & (cy > 0.35)
    left = cx < 0.4
    right = cx > 0.6
    largest = int(np.argmax(area))

    feat[0] = min(len(tracks) / 32.0, 1.0)
    feat[1] = _clip(float(conf.mean()), 0.0, 1.0)
    feat[2] = _clip(float(area.sum()), 0.0, 1.0)
    feat[3] = _clip(float(area[largest]), 0.0, 1.0)
    feat[4] = _clip(float(cx[largest] * 2.0 - 1.0), -1.0, 1.0)
    feat[5] = _clip(float(cy[largest] * 2.0 - 1.0), -1.0, 1.0)
    feat[6] = _clip(float(area[front].sum()) if front.any() else 0.0, 0.0, 1.0)
    feat[7] = _clip(float(area[left].sum()) if left.any() else 0.0, 0.0, 1.0)
    feat[8] = _clip(float(area[right].sum()) if right.any() else 0.0, 0.0, 1.0)
    feat[9] = _clip(float(age.mean()) / 30.0, 0.0, 1.0)
    feat[10] = _clip(float(np.max(conf * area)), 0.0, 1.0)
    feat[11] = 1.0
    return feat


def pack_icare_record(record: dict[str, Any]) -> np.ndarray:
    base = pack_record(record)
    return np.concatenate([base, pack_radar(record), pack_midas(record), pack_tracks(record)]).astype(np.float32)


def command_to_vector(command: dict[str, Any]) -> np.ndarray:
    vec = np.zeros(ICARE_ACTION_DIM, dtype=np.float32)
    vec[0] = _clip(_finite(command.get("north_mps")), -1.0, 1.0)
    vec[1] = _clip(_finite(command.get("east_mps")), -1.0, 1.0)
    vec[2] = _clip(_finite(command.get("down_mps_ned")), -1.0, 1.0)
    vec[3] = _clip(_finite(command.get("yaw_rate_rad_s")), -1.0, 1.0)
    actuators = command.get("actuators") or {}
    motors = list(actuators.get("motors") or [])
    for idx in range(4):
        vec[4 + idx] = _clip(_finite(motors[idx] if idx < len(motors) else 0.0), 0.0, 1.0)
    servos = actuators.get("servos") or {}
    for offset, name in enumerate(ICARE_ACTION_NAMES[8:], start=8):
        vec[offset] = _clip(_finite(servos.get(name), 0.0), -1.0, 1.0)
    return vec


def vector_to_command(vector: np.ndarray, mode_id: int, complete_prob: float) -> dict[str, Any]:
    vec = np.asarray(vector, dtype=np.float32)
    vec = np.pad(vec, (0, max(0, ICARE_ACTION_DIM - vec.shape[0])))[:ICARE_ACTION_DIM]
    return {
        "mode": ID_TO_ICARE_MODE.get(int(mode_id), "hold"),
        "mission_complete_probability": _clip(float(complete_prob), 0.0, 1.0),
        "velocity": {
            "north_mps": _clip(float(vec[0]), -1.0, 1.0),
            "east_mps": _clip(float(vec[1]), -1.0, 1.0),
            "down_mps_ned": _clip(float(vec[2]), -1.0, 1.0),
            "yaw_rate_rad_s": _clip(float(vec[3]), -1.0, 1.0),
        },
        "actuators": {
            "motors": [_clip(float(value), 0.0, 1.0) for value in vec[4:8]],
            "servos": {
                name: _clip(float(vec[idx]), -1.0, 1.0)
                for idx, name in enumerate(ICARE_ACTION_NAMES[8:], start=8)
            },
        },
    }


def bounded_command(command: dict[str, Any]) -> dict[str, Any]:
    """Deterministic guardrail between Icare and MAVLink/actuators."""

    out = dict(command)
    velocity = dict(out.get("velocity") or {})
    velocity["north_mps"] = _clip(_finite(velocity.get("north_mps")), -1.0, 1.0)
    velocity["east_mps"] = _clip(_finite(velocity.get("east_mps")), -1.0, 1.0)
    velocity["down_mps_ned"] = _clip(_finite(velocity.get("down_mps_ned")), -1.0, 1.0)
    velocity["yaw_rate_rad_s"] = _clip(_finite(velocity.get("yaw_rate_rad_s")), -1.0, 1.0)
    actuators = dict(out.get("actuators") or {})
    motors = [_clip(_finite(v), 0.0, 1.0) for v in list(actuators.get("motors") or [])[:4]]
    motors += [0.0] * (4 - len(motors))
    servos_in = actuators.get("servos") or {}
    servos = {name: _clip(_finite(servos_in.get(name)), -1.0, 1.0) for name in ICARE_ACTION_NAMES[8:]}
    out["velocity"] = velocity
    out["actuators"] = {"motors": motors, "servos": servos}
    out["guardrail"] = {
        "schema": "hesia.icare.guardrail.v1",
        "bounded": True,
        "direct_actuator_output": False,
    }
    return out
