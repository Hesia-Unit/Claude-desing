"""YOLO11 segmentation post-processing with stable track IDs.

This module is deliberately independent from the existing Ajax async pipeline.
It can process still images or ordered frames and emits compact, clean summaries
for Icare and the benchmark.  The tracker is a small IoU matcher so it has no
extra runtime dependency; it can later be replaced by a C++ implementation if
profiling shows this Python pass is the bottleneck.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import cv2
import numpy as np


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


@dataclass
class Track:
    track_id: int
    cls_id: int
    class_name: str
    bbox: tuple[float, float, float, float]
    confidence: float
    mask_area_fraction: float
    age: int = 1
    last_seen: int = 0


def iou(a: tuple[float, float, float, float], b: tuple[float, float, float, float]) -> float:
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    inter = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    area_a = max(0.0, ax2 - ax1) * max(0.0, ay2 - ay1)
    area_b = max(0.0, bx2 - bx1) * max(0.0, by2 - by1)
    denom = area_a + area_b - inter
    return inter / denom if denom > 1e-9 else 0.0


class SimpleIoUTracker:
    def __init__(self, iou_threshold: float = 0.25, max_missing: int = 8) -> None:
        self.iou_threshold = iou_threshold
        self.max_missing = max_missing
        self.next_id = 1
        self.tracks: list[Track] = []

    def update(self, detections: list[Track]) -> list[Track]:
        assigned_tracks: set[int] = set()
        assigned_detections: set[int] = set()
        for det_idx, det in enumerate(detections):
            best_idx = -1
            best_iou = 0.0
            for trk_idx, trk in enumerate(self.tracks):
                if trk_idx in assigned_tracks or trk.cls_id != det.cls_id:
                    continue
                score = iou(trk.bbox, det.bbox)
                if score > best_iou:
                    best_iou = score
                    best_idx = trk_idx
            if best_idx >= 0 and best_iou >= self.iou_threshold:
                trk = self.tracks[best_idx]
                det.track_id = trk.track_id
                det.age = trk.age + 1
                det.last_seen = 0
                self.tracks[best_idx] = det
                assigned_tracks.add(best_idx)
                assigned_detections.add(det_idx)

        for det_idx, det in enumerate(detections):
            if det_idx in assigned_detections:
                continue
            det.track_id = self.next_id
            self.next_id += 1
            self.tracks.append(det)

        for idx, trk in enumerate(self.tracks):
            if idx not in assigned_tracks:
                trk.last_seen += 1
        self.tracks = [trk for trk in self.tracks if trk.last_seen <= self.max_missing]
        return [trk for trk in self.tracks if trk.last_seen == 0]


def detections_from_result(result: Any) -> list[Track]:
    names = getattr(result, "names", {}) or {}
    boxes = getattr(result, "boxes", None)
    masks = getattr(result, "masks", None)
    if boxes is None:
        return []

    xyxy = boxes.xyxy.detach().cpu().numpy() if getattr(boxes, "xyxy", None) is not None else np.zeros((0, 4))
    conf = boxes.conf.detach().cpu().numpy() if getattr(boxes, "conf", None) is not None else np.zeros((len(xyxy),))
    cls = boxes.cls.detach().cpu().numpy().astype(int) if getattr(boxes, "cls", None) is not None else np.zeros((len(xyxy),), dtype=int)
    mask_area = np.zeros((len(xyxy),), dtype=np.float32)
    if masks is not None and getattr(masks, "data", None) is not None:
        data = masks.data.detach().float().cpu().numpy()
        image_area = float(data.shape[-1] * data.shape[-2]) if data.size else 1.0
        for idx in range(min(len(mask_area), data.shape[0])):
            mask_area[idx] = float(data[idx].sum() / image_area)

    detections = []
    for idx, box in enumerate(xyxy):
        detections.append(
            Track(
                track_id=0,
                cls_id=int(cls[idx]),
                class_name=str(names.get(int(cls[idx]), int(cls[idx]))),
                bbox=(float(box[0]), float(box[1]), float(box[2]), float(box[3])),
                confidence=float(conf[idx]),
                mask_area_fraction=float(mask_area[idx]),
            )
        )
    return detections


def track_to_json(track: Track, width: int, height: int) -> dict[str, Any]:
    x1, y1, x2, y2 = track.bbox
    area = max(0.0, x2 - x1) * max(0.0, y2 - y1) / max(float(width * height), 1.0)
    cx = ((x1 + x2) / 2.0) / max(width, 1)
    cy = ((y1 + y2) / 2.0) / max(height, 1)
    return {
        "track_id": track.track_id,
        "class_id": track.cls_id,
        "class_name": track.class_name,
        "confidence": track.confidence,
        "bbox_xyxy": [x1, y1, x2, y2],
        "bbox_area_fraction": area,
        "mask_area_fraction": track.mask_area_fraction,
        "center": {"x": cx, "y": cy},
        "sector": "left" if cx < 0.4 else "right" if cx > 0.6 else "center",
        "age": track.age,
        "last_seen": track.last_seen,
    }


def summarize_tracks(tracks: list[Track], width: int, height: int, source: str) -> dict[str, Any]:
    encoded = [track_to_json(track, width, height) for track in tracks]
    front = sum(item["mask_area_fraction"] for item in encoded if item["sector"] == "center")
    left = sum(item["mask_area_fraction"] for item in encoded if item["sector"] == "left")
    right = sum(item["mask_area_fraction"] for item in encoded if item["sector"] == "right")
    return {
        "schema": "hesia.yolo11.track_summary.v1",
        "source": source,
        "image_size": {"width": width, "height": height},
        "track_count": len(encoded),
        "center_obstacle_fraction": min(front, 1.0),
        "left_obstacle_fraction": min(left, 1.0),
        "right_obstacle_fraction": min(right, 1.0),
        "tracks": encoded,
    }


def list_images(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    return sorted(item for item in path.rglob("*") if item.suffix.lower() in IMAGE_EXTS)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, default=Path("ml/Model/yolo11m-seg.pt"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--device", default="0")
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--conf", type=float, default=0.25)
    parser.add_argument("--iou", type=float, default=0.45)
    args = parser.parse_args()

    from ultralytics import YOLO

    model = YOLO(str(args.model))
    tracker = SimpleIoUTracker()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as handle:
        for frame_idx, image in enumerate(list_images(args.input)):
            img = cv2.imread(str(image))
            if img is None:
                continue
            height, width = img.shape[:2]
            result = model.predict(str(image), imgsz=args.imgsz, conf=args.conf, iou=args.iou, device=args.device, verbose=False)[0]
            tracks = tracker.update(detections_from_result(result))
            payload = {
                "frame_index": frame_idx,
                "image": str(image),
                "summary": summarize_tracks(tracks, width, height, "yolo11m_seg_iou_tracker_v1"),
            }
            handle.write(json.dumps(payload, sort_keys=True) + "\n")
    print(json.dumps({"out": str(args.out), "frames": len(list_images(args.input))}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
