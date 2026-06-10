"""Run YOLO11m-seg and export compact scene summaries for the policy dataset."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np


DEFAULT_MODEL = Path(__file__).resolve().parents[1] / "Model" / "yolo11m-seg.pt"


def summarize_result(result: Any) -> dict[str, Any]:
    names = getattr(result, "names", {}) or {}
    boxes = getattr(result, "boxes", None)
    masks = getattr(result, "masks", None)
    instances = int(len(boxes)) if boxes is not None else 0
    confidences = boxes.conf.detach().cpu().numpy().tolist() if boxes is not None and getattr(boxes, "conf", None) is not None else []
    classes = boxes.cls.detach().cpu().numpy().astype(int).tolist() if boxes is not None and getattr(boxes, "cls", None) is not None else []

    coverage: dict[str, float] = {}
    obstacle_fraction = 0.0
    if masks is not None and getattr(masks, "data", None) is not None:
        data = masks.data.detach().float().cpu().numpy()
        image_area = float(data.shape[-1] * data.shape[-2]) if data.size else 1.0
        for idx, cls_id in enumerate(classes[: data.shape[0]]):
            label = str(names.get(cls_id, cls_id))
            frac = float(data[idx].sum() / image_area)
            coverage[label] = coverage.get(label, 0.0) + frac
        obstacle_fraction = float(min(sum(coverage.values()), 1.0))

    return {
        "instances": instances,
        "mean_confidence": float(np.mean(confidences)) if confidences else 0.0,
        "mask_coverage": coverage,
        "obstacle_fraction": obstacle_fraction,
        "safe_surface_fraction": float(max(0.0, 1.0 - obstacle_fraction)),
        "center_obstacle_fraction": 0.0,
        "left_obstacle_fraction": 0.0,
        "right_obstacle_fraction": 0.0,
        "horizon_clearance": 1.0,
        "motion_blur_score": 0.0,
        "exposure_risk": 0.0,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--device", default="0")
    parser.add_argument("--imgsz", type=int, default=640)
    args = parser.parse_args()

    from ultralytics import YOLO

    model = YOLO(str(args.model))
    results = model.predict(str(args.image), imgsz=args.imgsz, device=args.device, verbose=False)
    payload = {
        "schema": "hesia.autonomy.yolo11m_seg.summary.v1",
        "model": str(args.model),
        "image": str(args.image),
        "summary": summarize_result(results[0]),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps(payload["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
