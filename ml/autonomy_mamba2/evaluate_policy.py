"""Evaluate a trained Mamba-2 autonomy policy checkpoint."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import time
from pathlib import Path
from typing import Any

import torch
from torch.utils.data import DataLoader

from .controller import Mamba2DecisionPolicy, Mamba2PolicyConfig, count_parameters
from .train_policy import AutonomyJsonlDataset


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_model(checkpoint: Path, device: torch.device) -> Mamba2DecisionPolicy:
    blob: dict[str, Any] = torch.load(checkpoint, map_location=device, weights_only=False)
    cfg_data = blob.get("config") or {}
    allowed = {field.name for field in dataclasses.fields(Mamba2PolicyConfig)}
    cfg = Mamba2PolicyConfig(**{key: value for key, value in cfg_data.items() if key in allowed})
    model = Mamba2DecisionPolicy(cfg).to(device).eval()
    model.load_state_dict(blob["model"])
    return model


@torch.no_grad()
def evaluate(model: Mamba2DecisionPolicy, loader: DataLoader, device: torch.device) -> dict[str, float]:
    total_mae = 0.0
    total_smooth_l1 = 0.0
    total_correct = 0
    total_tokens = 0
    for features, command, mode in loader:
        features = features.to(device)
        command = command.to(device)
        mode = mode.to(device)
        out = model(features)
        total_mae += float(torch.abs(out["command"] - command).mean().cpu()) * mode.numel()
        total_smooth_l1 += float(torch.nn.functional.smooth_l1_loss(out["command"], command).cpu()) * features.shape[0]
        total_correct += int((out["mode_logits"].argmax(dim=-1) == mode).sum().cpu())
        total_tokens += int(mode.numel())
    return {
        "command_mae": total_mae / max(total_tokens, 1),
        "command_smooth_l1": total_smooth_l1 / max(len(loader.dataset), 1),  # type: ignore[arg-type]
        "mode_accuracy": total_correct / max(total_tokens, 1),
    }


@torch.no_grad()
def latency(model: Mamba2DecisionPolicy, device: torch.device, sequence_len: int, repeats: int) -> dict[str, float]:
    x = torch.randn(1, sequence_len, model.cfg.input_dim, device=device)
    if device.type == "cuda":
        for _ in range(10):
            model(x)
        torch.cuda.synchronize()
    start = time.perf_counter()
    for _ in range(repeats):
        model(x)
    if device.type == "cuda":
        torch.cuda.synchronize()
    seq_ms = (time.perf_counter() - start) * 1000.0 / repeats

    state = None
    step_x = torch.randn(1, model.cfg.input_dim, device=device)
    if device.type == "cuda":
        for _ in range(10):
            step = model.step(step_x, state)
            state = step["states"]  # type: ignore[assignment]
        torch.cuda.synchronize()
    start = time.perf_counter()
    for _ in range(repeats):
        step = model.step(step_x, state)
        state = step["states"]  # type: ignore[assignment]
    if device.type == "cuda":
        torch.cuda.synchronize()
    step_ms = (time.perf_counter() - start) * 1000.0 / repeats
    return {"sequence_forward_ms": seq_ms, "recurrent_step_ms": step_ms, "repeats": float(repeats)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sequence-len", type=int, default=12)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--latency-repeats", type=int, default=200)
    args = parser.parse_args()

    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    dataset = AutonomyJsonlDataset(args.dataset, args.sequence_len)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=False, drop_last=False)
    model = load_model(args.checkpoint, device)
    payload = {
        "schema": "hesia.autonomy.evaluate_policy.v1",
        "status": "passed",
        "device": str(device),
        "cuda": torch.cuda.is_available(),
        "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
        "checkpoint": str(args.checkpoint),
        "checkpoint_bytes": args.checkpoint.stat().st_size,
        "checkpoint_sha256": sha256(args.checkpoint),
        "dataset": str(args.dataset),
        "dataset_records": len(dataset.records),
        "sequence_len": args.sequence_len,
        "parameter_count": count_parameters(model),
        "metrics": evaluate(model, loader, device),
        "latency": latency(model, device, args.sequence_len, args.latency_repeats),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
