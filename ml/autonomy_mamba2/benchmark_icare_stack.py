"""Benchmark the local C: Icare stack.

This does not modify the Ajax async pipeline.  It runs a local sequential
benchmark over dataset frames, with logs/DB artifacts written under F: or
artifacts/icare.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any

import cv2
import numpy as np
import torch

from .features_icare import bounded_command, pack_icare_record, vector_to_command
from .icare_controller import IcareConfig, IcareDecisionModel
from .train_icare import build_mission_embeddings
from .yolo11_segment import summarize_result
from .yolo_track_postprocess import SimpleIoUTracker, detections_from_result, summarize_tracks


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    idx = min(len(values) - 1, max(0, int(round((len(values) - 1) * q))))
    return float(values[idx])


def stats(values: list[float]) -> dict[str, float]:
    return {
        "count": float(len(values)),
        "mean_ms": float(statistics.mean(values)) if values else 0.0,
        "p50_ms": percentile(values, 0.50),
        "p95_ms": percentile(values, 0.95),
        "max_ms": max(values) if values else 0.0,
    }


def nvidia_smi() -> dict[str, Any]:
    try:
        out = subprocess.check_output(
            [
                "nvidia-smi",
                "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw,clocks.sm",
                "--format=csv,noheader,nounits",
            ],
            text=True,
            timeout=3,
        ).strip()
        util, mem_used, mem_total, temp, power, clock = [item.strip() for item in out.split(",")[:6]]
        return {
            "gpu_util_pct": float(util),
            "memory_used_mb": float(mem_used),
            "memory_total_mb": float(mem_total),
            "temperature_c": float(temp),
            "power_w": float(power),
            "sm_clock_mhz": float(clock),
        }
    except Exception as exc:
        return {"error": type(exc).__name__ + ": " + str(exc)[:120]}


def load_icare(checkpoint: Path, device: torch.device, fp16: bool) -> tuple[IcareDecisionModel, dict[str, Any]]:
    blob = torch.load(checkpoint, map_location=device, weights_only=False)
    cfg_data = blob.get("config") or {}
    allowed = {field.name for field in dataclasses.fields(IcareConfig)}
    cfg = IcareConfig(**{key: value for key, value in cfg_data.items() if key in allowed})
    model = IcareDecisionModel(cfg).to(device).eval()
    model.load_state_dict(blob["model"])
    if fp16 and device.type == "cuda":
        model = model.half()
    return model, blob


def load_midas(device: torch.device, fp16: bool):
    model = torch.hub.load("intel-isl/MiDaS", "MiDaS_small", trust_repo=True).to(device).eval()
    transforms = torch.hub.load("intel-isl/MiDaS", "transforms", trust_repo=True)
    if fp16 and device.type == "cuda":
        model = model.half()
    return model, transforms.small_transform


@torch.no_grad()
def midas_summary(model: torch.nn.Module, transform: Any, image_bgr: np.ndarray, device: torch.device, fp16: bool) -> dict[str, Any]:
    image_rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    tensor = transform(image_rgb).to(device)
    if fp16 and device.type == "cuda":
        tensor = tensor.half()
    pred = model(tensor)
    pred = torch.nn.functional.interpolate(
        pred.unsqueeze(1),
        size=image_bgr.shape[:2],
        mode="bicubic",
        align_corners=False,
    ).squeeze()
    depth = pred.float().detach().cpu().numpy()
    dmin = float(np.min(depth))
    dmax = float(np.max(depth))
    norm = (depth - dmin) / max(dmax - dmin, 1e-6)
    h, w = norm.shape
    center = norm[h // 3 : 2 * h // 3, w // 3 : 2 * w // 3]
    left = norm[:, : w // 2]
    right = norm[:, w // 2 :]
    top = norm[: h // 3, :]
    bottom = norm[2 * h // 3 :, :]
    return {
        "schema": "hesia.midas.summary.v1",
        "source": "torchhub_midas_small",
        "depth_mean": float(norm.mean()),
        "depth_min": float(norm.min()),
        "center_depth_min": float(center.min()) if center.size else 0.0,
        "left_depth_min": float(left.min()) if left.size else 0.0,
        "right_depth_min": float(right.min()) if right.size else 0.0,
        "horizon_clearance": float(top.mean()) if top.size else 1.0,
        "vertical_gradient": float(np.tanh((top.mean() - bottom.mean()) * 5.0)) if top.size and bottom.size else 0.0,
        "asymmetry": float(np.tanh((left.mean() - right.mean()) * 5.0)) if left.size and right.size else 0.0,
    }


def yolo_outputs(yolo_model: Any, tracker: SimpleIoUTracker, image_path: Path, image_bgr: np.ndarray, device_arg: str, fp16: bool) -> tuple[dict[str, Any], dict[str, Any]]:
    result = yolo_model.predict(str(image_path), imgsz=640, device=device_arg, half=fp16, verbose=False)[0]
    summary = summarize_result(result)
    h, w = image_bgr.shape[:2]
    tracks = tracker.update(detections_from_result(result))
    track_summary = summarize_tracks(tracks, w, h, "yolo11m_seg_iou_tracker_v1")
    summary["center_obstacle_fraction"] = track_summary["center_obstacle_fraction"]
    summary["left_obstacle_fraction"] = track_summary["left_obstacle_fraction"]
    summary["right_obstacle_fraction"] = track_summary["right_obstacle_fraction"]
    return summary, track_summary


def load_mission_embeddings(records: list[dict[str, Any]], train_cache: Path, language_model: Path, device: torch.device) -> dict[str, torch.Tensor]:
    texts = sorted(set(str(record.get("mission_text", "")) for record in records))
    if train_cache.exists():
        blob = torch.load(train_cache, map_location="cpu", weights_only=False)
        embeddings = blob.get("embeddings") or {}
        if all(text in embeddings for text in texts):
            return embeddings
    return build_mission_embeddings(texts, language_model, device, 96)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, default=Path("F:/Set-Donner/datasets/icare_autonomy_v1/test.jsonl"))
    parser.add_argument("--checkpoint", type=Path, default=Path("artifacts/icare/train_1_3b/icare_policy.pt"))
    parser.add_argument("--out", type=Path, default=Path("artifacts/icare/benchmark_5min.json"))
    parser.add_argument("--log-jsonl", type=Path, default=Path("F:/Set-Donner/datasets/icare_autonomy_v1/benchmark_5min_log.jsonl"))
    parser.add_argument("--duration-sec", type=float, default=300.0)
    parser.add_argument("--warmup-frames", type=int, default=5)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--fp16", action="store_true", default=True)
    args = parser.parse_args()

    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    fp16 = bool(args.fp16 and device.type == "cuda")
    records = load_jsonl(args.dataset)
    if not records:
        raise SystemExit("empty benchmark dataset")

    from ultralytics import YOLO

    yolo = YOLO("ml/Model/yolo11m-seg.pt")
    midas, midas_transform = load_midas(device, fp16)
    icare, ckpt = load_icare(args.checkpoint, device, fp16)
    language_model = Path(ckpt.get("language_model", "F:/Set-Donner/models/AntonV__mamba2-1.3b-hf"))
    mission_cache = load_mission_embeddings(records, Path("artifacts/icare/train_1_3b/mission_embeddings.pt"), language_model, device)
    tracker = SimpleIoUTracker()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.log_jsonl.parent.mkdir(parents=True, exist_ok=True)

    # Warm CUDA kernels, MiDaS and YOLO without counting the first-use spikes.
    for idx in range(min(args.warmup_frames, len(records))):
        image_path = Path(records[idx].get("frame_path", ""))
        image = cv2.imread(str(image_path))
        if image is None:
            continue
        _ = midas_summary(midas, midas_transform, image, device, fp16)
        _ = yolo_outputs(yolo, tracker, image_path, image, "0" if device.type == "cuda" else "cpu", fp16)
        if device.type == "cuda":
            torch.cuda.synchronize()

    timings: dict[str, list[float]] = {"midas": [], "yolo": [], "feature": [], "icare": [], "total": []}
    gpu_samples = []
    commands = []
    states = None
    start = time.perf_counter()
    next_gpu = start
    frames = 0
    with args.log_jsonl.open("w", encoding="utf-8") as log:
        while time.perf_counter() - start < args.duration_sec:
            record = dict(records[frames % len(records)])
            image_path = Path(record.get("frame_path", ""))
            image = cv2.imread(str(image_path))
            if image is None:
                frames += 1
                continue
            t_total = time.perf_counter()

            t = time.perf_counter()
            record["midas"] = midas_summary(midas, midas_transform, image, device, fp16)
            if device.type == "cuda":
                torch.cuda.synchronize()
            timings["midas"].append((time.perf_counter() - t) * 1000.0)

            t = time.perf_counter()
            yolo_summary, track_summary = yolo_outputs(yolo, tracker, image_path, image, "0" if device.type == "cuda" else "cpu", fp16)
            if device.type == "cuda":
                torch.cuda.synchronize()
            record["yolo"] = {**(record.get("yolo") or {}), **yolo_summary}
            record["yolo_tracks"] = track_summary
            timings["yolo"].append((time.perf_counter() - t) * 1000.0)

            t = time.perf_counter()
            feature = torch.tensor(pack_icare_record(record), dtype=torch.float16 if fp16 else torch.float32, device=device).unsqueeze(0)
            mission_text = str(record.get("mission_text", ""))
            mission = mission_cache[mission_text].to(device=device, dtype=torch.float16 if fp16 else torch.float32).unsqueeze(0)
            timings["feature"].append((time.perf_counter() - t) * 1000.0)

            t = time.perf_counter()
            with torch.no_grad():
                out = icare.step(feature, mission, states=states)
            states = out["states"]
            if device.type == "cuda":
                torch.cuda.synchronize()
            raw = out["command"].detach().float().cpu().numpy()[0]
            mode = int(out["mode_logits"].argmax(dim=-1).detach().cpu()[0])
            complete_prob = float(torch.sigmoid(out["complete_logit"]).detach().cpu()[0])
            command = bounded_command(vector_to_command(raw, mode, complete_prob))
            timings["icare"].append((time.perf_counter() - t) * 1000.0)
            timings["total"].append((time.perf_counter() - t_total) * 1000.0)
            commands.append(command["mode"])

            now = time.perf_counter()
            if now >= next_gpu:
                gpu_samples.append(nvidia_smi())
                next_gpu = now + 1.0
            log.write(json.dumps({"frame": frames, "image": str(image_path), "command": command, "latency_ms": {k: v[-1] for k, v in timings.items() if v}}, sort_keys=True) + "\n")
            frames += 1

    elapsed = time.perf_counter() - start
    payload = {
        "schema": "hesia.icare.stack_benchmark.v1",
        "status": "passed",
        "duration_sec": elapsed,
        "target_duration_sec": args.duration_sec,
        "frames": frames,
        "fps": frames / max(elapsed, 1e-6),
        "device": str(device),
        "fp16": fp16,
        "checkpoint": str(args.checkpoint),
        "dataset": str(args.dataset),
        "latency": {key: stats(values) for key, values in timings.items()},
        "gpu_samples": gpu_samples,
        "modes_seen": {mode: commands.count(mode) for mode in sorted(set(commands))},
        "log_jsonl": str(args.log_jsonl),
        "notes": [
            "Local sequential benchmark; Ajax asynchronous perception pipeline was not modified.",
            "Mission language embedding is cached per mission text and not recomputed each frame.",
        ],
    }
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
