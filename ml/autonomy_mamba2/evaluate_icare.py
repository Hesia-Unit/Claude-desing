"""Evaluate an Icare checkpoint."""

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

from .icare_controller import IcareConfig, IcareDecisionModel, icare_parameter_count
from .train_icare import IcareJsonlDataset, build_mission_embeddings, collate_with_embeddings, evaluate


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_icare(checkpoint: Path, device: torch.device) -> tuple[IcareDecisionModel, dict[str, Any]]:
    blob = torch.load(checkpoint, map_location=device, weights_only=False)
    cfg_data = blob.get("config") or {}
    allowed = {field.name for field in dataclasses.fields(IcareConfig)}
    cfg = IcareConfig(**{key: value for key, value in cfg_data.items() if key in allowed})
    model = IcareDecisionModel(cfg).to(device).eval()
    model.load_state_dict(blob["model"])
    return model, blob


@torch.no_grad()
def latency(model: IcareDecisionModel, mission_dim: int, device: torch.device, repeats: int, sequence_len: int) -> dict[str, float]:
    features = torch.randn(1, sequence_len, model.cfg.feature_dim, device=device)
    mission = torch.randn(1, mission_dim, device=device)
    if device.type == "cuda":
        for _ in range(10):
            model(features, mission)
        torch.cuda.synchronize()
    start = time.perf_counter()
    for _ in range(repeats):
        model(features, mission)
    if device.type == "cuda":
        torch.cuda.synchronize()
    seq_ms = (time.perf_counter() - start) * 1000.0 / repeats

    state = None
    step_feature = torch.randn(1, model.cfg.feature_dim, device=device)
    if device.type == "cuda":
        for _ in range(10):
            out = model.step(step_feature, mission, states=state)
            state = out["states"]
        torch.cuda.synchronize()
    start = time.perf_counter()
    for _ in range(repeats):
        out = model.step(step_feature, mission, states=state)
        state = out["states"]
    if device.type == "cuda":
        torch.cuda.synchronize()
    step_ms = (time.perf_counter() - start) * 1000.0 / repeats
    return {"sequence_forward_ms": seq_ms, "recurrent_step_ms": step_ms, "repeats": float(repeats)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, default=Path("artifacts/icare/train_1_3b/icare_policy.pt"))
    parser.add_argument("--dataset", type=Path, default=Path("F:/Set-Donner/datasets/icare_autonomy_v1/test.jsonl"))
    parser.add_argument("--out", type=Path, default=Path("artifacts/icare/evaluate_test.json"))
    parser.add_argument("--sequence-len", type=int, default=12)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--latency-repeats", type=int, default=200)
    args = parser.parse_args()

    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    model, blob = load_icare(args.checkpoint, device)
    dataset = IcareJsonlDataset(args.dataset, args.sequence_len)
    texts = [str(record.get("mission_text", "")) for record in dataset.records]
    language_model = Path(blob.get("language_model", "F:/Set-Donner/models/AntonV__mamba2-1.3b-hf"))
    embeddings = build_mission_embeddings(texts, language_model, device, 96)
    loader = DataLoader(
        dataset,
        batch_size=args.batch_size,
        shuffle=False,
        drop_last=False,
        collate_fn=lambda batch: collate_with_embeddings(batch, embeddings),
    )
    metrics = evaluate(model, loader, device)
    payload = {
        "schema": "hesia.icare.evaluate.v1",
        "status": "passed",
        "checkpoint": str(args.checkpoint),
        "checkpoint_sha256": sha256(args.checkpoint),
        "dataset": str(args.dataset),
        "records": len(dataset.records),
        "device": str(device),
        "cuda": torch.cuda.is_available(),
        "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
        "parameter_count": icare_parameter_count(model),
        "metrics": metrics,
        "latency": latency(model, model.cfg.mission_dim, device, args.latency_repeats, args.sequence_len),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
