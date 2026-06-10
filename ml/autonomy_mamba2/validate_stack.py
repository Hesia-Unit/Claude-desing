"""Smoke validation for the YOLO11m-seg + Mamba-2 autonomy base."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import torch

from .controller import Mamba2DecisionPolicy, Mamba2PolicyConfig, count_parameters
from .features import FEATURE_DIM
from .scenarios import build_manifest


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--yolo", type=Path, default=Path("ml/Model/yolo11m-seg.pt"))
    parser.add_argument("--out", type=Path, default=Path("artifacts/jetson_ajax/phase12_autonomy_stack_validation.json"))
    parser.add_argument("--device", default="cuda")
    args = parser.parse_args()

    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    cfg = Mamba2PolicyConfig(input_dim=FEATURE_DIM)
    model = Mamba2DecisionPolicy(cfg).to(device).eval()
    x = torch.randn(2, 12, FEATURE_DIM, device=device)
    with torch.no_grad():
        out = model(x)
        step = model.step(x[:, -1])

    scenario_manifest = build_manifest(repeats=2)
    payload = {
        "schema": "hesia.autonomy.stack_validation.v1",
        "status": "passed",
        "device": str(device),
        "cuda": torch.cuda.is_available(),
        "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
        "yolo": {
            "path": str(args.yolo),
            "exists": args.yolo.exists(),
            "bytes": args.yolo.stat().st_size if args.yolo.exists() else 0,
            "sha256": sha256(args.yolo) if args.yolo.exists() else None,
        },
        "controller": {
            "config": cfg.to_dict(),
            "parameter_count": count_parameters(model),
            "command_shape": list(out["command"].shape),
            "mode_logits_shape": list(out["mode_logits"].shape),
            "step_command_shape": list(step["command"].shape),
            "state_layers": len(step["states"]),
        },
        "scenario_manifest": {
            "scenario_count": scenario_manifest["scenario_count"],
            "templates": scenario_manifest["templates"],
        },
    }
    payload["status"] = "passed" if payload["yolo"]["exists"] and payload["controller"]["command_shape"] == [2, 12, 4] else "failed"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps(payload, indent=2))
    return 0 if payload["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
