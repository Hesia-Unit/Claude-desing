# HESIA ML Workspace

## Active Direction

The previous HESIA-M2B multimodal R&D track is no longer the active autonomy path.

The current architecture is:

- **Perception:** `ml/Model/yolo11m-seg.pt`
- **State source:** Pixhawk/SITL telemetry, plus mission target variables
- **Decision model:** `ml/autonomy_mamba2`, a linear-time Mamba-2 style policy that outputs velocity and yaw-rate proposals
- **Training data:** simulator-acquired image/video frames paired with Pixhawk variables, YOLO11 segmentation summaries, mission goals, disturbances, and expert commands

Training is intentionally blocked until the simulator dataset has real expert labels. Placeholder samples are accepted only for dry-run shape validation.

## Active Files

- `ml/Model/yolo11m-seg.pt`
- `ml/autonomy_mamba2/controller.py`
- `ml/autonomy_mamba2/features.py`
- `ml/autonomy_mamba2/scenarios.py`
- `ml/autonomy_mamba2/yolo11_segment.py`
- `ml/autonomy_mamba2/collect_dataset.py`
- `ml/autonomy_mamba2/train_policy.py`
- `ml/autonomy_mamba2/validate_stack.py`

## Dataset Layout

Target root:

```text
F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba/
```

Expected files:

- `scenario_manifest.json`
- `train.jsonl`
- `val.jsonl`
- `test.jsonl`
- simulator frame/video folders
- optional YOLO summary JSONL files

Each training sample must contain:

- `frame_path`
- `telemetry`
- `yolo`
- `goal`
- `disturbance`
- `expert_command`
- `scenario_id`

## Validated Commands

Download/verify YOLO11m-seg:

```bash
python - <<'PY'
from ultralytics import YOLO
YOLO("ml/Model/yolo11m-seg.pt")
PY
```

Validate the active autonomy stack:

```bash
python -m ml.autonomy_mamba2.validate_stack --device cuda
```

Generate constrained scenario manifest:

```bash
python -m ml.autonomy_mamba2.scenarios \
  --out F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba/scenario_manifest.json \
  --repeats 24
```

Run YOLO11m-seg summary for one image:

```bash
python -m ml.autonomy_mamba2.yolo11_segment \
  --image F:/Set-Donner/datasets/segmentation/ADE20K/ADEChallengeData2016/images/validation/ADE_val_00000001.jpg \
  --out artifacts/jetson_ajax/phase12_yolo11m_seg_summary.json \
  --device 0
```

Dry-run the policy trainer without learning:

```bash
python -m ml.autonomy_mamba2.train_policy \
  --dataset F:/Set-Donner/datasets/hesia_autonomy_yolo_mamba/smoke_train.jsonl \
  --out-dir artifacts/jetson_ajax/phase12_policy_train_dry_run \
  --sequence-len 12 \
  --batch-size 4 \
  --dry-run \
  --allow-placeholder
```

## Archived Work

The deprecated HESIA-M2B code, old Alpha artifacts, and M2B-only viewers were moved to:

```text
F:/Jetson-Ajax/archives/deprecated_hesia_m2b_20260525_220602/
```

Archive manifest:

```text
artifacts/jetson_ajax/phase12_hesia_m2b_archive_manifest.json
```
