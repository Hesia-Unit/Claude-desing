# HESIA M2B Visualizer

## Purpose

This document ships an executable viewer for the embedded multimodal candidate so engineers can see:

- what the model receives
- what the model highlights
- what action chunk and risk it predicts

Script:

- [hesia_m2b_viewer.py](/C:/Users/matis/Documents/Hesia-Firmware/docs/scripts/hesia_m2b_viewer.py)

## What the viewer shows

For each frame, the generated video displays:

- RGB input seen by the model
- depth input seen by the model
- obstacle heatmap overlay
- goal heatmap overlay plus a HUD with:
  - risk score
  - first predicted action vector
  - run mode label

## Modes

### 1. Synthetic mode

This is the most faithful quick preview because it uses the exact synthetic state vector family used during training.

Example:

```bash
python docs/scripts/hesia_m2b_viewer.py
```

Useful knobs:

```bash
python docs/scripts/hesia_m2b_viewer.py --frames 24 --display-scale 4
```

Outputs by default:

- `docs/generated/hesia_m2b_viewer.mp4`
- `docs/generated/hesia_m2b_viewer_contact_sheet.jpg`
- `docs/generated/hesia_m2b_viewer_summary.json`

### 2. Runtime trace mode

This mode works on real traces collected by the drone runtime when:

- `HESIA_M2B_TRACE=1`

Recommended runtime knobs:

```bash
export HESIA_M2B_TRACE=1
export HESIA_M2B_TRACE_EVERY_N=15
```

Default trace location:

- `blackbox/m2b_dataset`

Expected structure:

- `blackbox/m2b_dataset/rgb`
- `blackbox/m2b_dataset/depth`
- `blackbox/m2b_dataset/preview`
- `blackbox/m2b_dataset/metadata.jsonl`

Then run:

```bash
python docs/scripts/hesia_m2b_viewer.py --dataset-dir blackbox/m2b_dataset
```

If the runtime capture is visually dense, increase the rendering scale:

```bash
python docs/scripts/hesia_m2b_viewer.py --dataset-dir blackbox/m2b_dataset --display-scale 4
```

## Important honesty note

Trace mode currently uses a **12-dimensional proxy state** reconstructed from the trace metadata because the compact policy has not yet been wired end-to-end to the real drone flight-state vector inside the executable.

That means:

- RGB, depth, and heatmaps are useful for visual inspection
- action outputs in trace mode are informative, but not yet final flight-control truth

Synthetic mode remains the cleanest way to inspect the current model in a fully aligned state space.

## Runtime trace collection

The clean pipeline now supports M2B trace export without touching the critical video transport path.

When enabled, it saves:

- original RGB frames
- normalized depth snapshots derived from MiDaS, with per-frame min/max logged
- preview composites
- metadata including `yolo_state` and `midas_state`

This gives the ML team a real capture format that can be replayed later with the visualizer.
