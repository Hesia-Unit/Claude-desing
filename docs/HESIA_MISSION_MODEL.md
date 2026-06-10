# HESIA Mission Multimodal Stack

## Purpose

This document covers the mission-capable multimodal branch added on top of the compact HESIA M2B candidate.

This branch targets:

- low-level actuator decisions for the Pixhawk path
- multi-stage mission execution
- onboard-state conditioning
- semantic and instance-oriented scene understanding

Core source files:

- [mission_model.py](/C:/Users/matis/Documents/Hesia-Firmware/ml/hesia_m2b/mission_model.py)
- [mission_simulator.py](/C:/Users/matis/Documents/Hesia-Firmware/ml/hesia_m2b/mission_simulator.py)
- [mission_train.py](/C:/Users/matis/Documents/Hesia-Firmware/ml/hesia_m2b/mission_train.py)
- [mission_train_closed_loop.py](/C:/Users/matis/Documents/Hesia-Firmware/ml/hesia_m2b/mission_train_closed_loop.py)
- [mission_rollout.py](/C:/Users/matis/Documents/Hesia-Firmware/ml/hesia_m2b/mission_rollout.py)
- [hesia_mission_viewer.py](/C:/Users/matis/Documents/Hesia-Firmware/docs/scripts/hesia_mission_viewer.py)

## What the mission model predicts

The mission branch predicts:

- `action`: 6 low-level actuator values
  - 4 motor power commands
  - 2 flap inclination commands
- `risk`: scalar mission hazard estimate
- `stage_logits`: predicted mission stage
- `semantic_logits`: dense semantic map
- `instance_masks`: fixed-slot instance masks
- `instance_logits`: class logits for each fixed slot

## Mission stages

The simulator uses a realistic multi-step mission abstraction:

1. takeoff
2. navigate to waypoint
3. inspect target
4. return home
5. land

Success requires:

- all stages completed
- safe return to home pad
- acceptable battery remaining
- no collision / no-fly-zone violation

## Onboard state

The state vector now includes real mission context, not just local geometry:

- position and velocity
- heading encoding
- battery estimate
- inspect-progress
- mission-progress
- desired altitude
- delta to active target
- delta to home
- delta to target object
- nearest-obstacle distance
- no-fly-zone clearance
- wind estimate
- flap state summary
- time remaining
- stage one-hot

This yields `state_dim = 33`.

## Simulator

The mission simulator is intentionally more operational than the original compact synthetic environment.

It models:

- 4 motor commands
- 2 flaps
- wind bias
- battery drain
- stage transitions
- waypoint / target / landing-pad / no-fly-zone semantics
- 3 obstacle instances

It is still a lightweight training simulator, not a full CFD or photorealistic stack.

For realistic multirotor dynamics transfer, the intended next layer remains RotorPy / Flightmare / Pegasus-class simulation.

## Training

### Supervised warm start

```bash
python -m ml.hesia_m2b.mission_train \
  --steps 120 \
  --batch-size 8 \
  --seq-len 6 \
  --ticket-rounds 1 \
  --ticket-prune-rate 0.15
```

### Closed-loop adaptation

```bash
python -m ml.hesia_m2b.mission_train_closed_loop \
  --checkpoint artifacts/mission_train_smoke.pt \
  --episodes 20 \
  --horizon 220 \
  --rounds 3 \
  --epochs-per-round 1 \
  --batch-size 8
```

### Rollout evaluation

```bash
python -m ml.hesia_m2b.mission_rollout \
  --checkpoint artifacts/mission_train_smoke.pt \
  --policy model
```

## Viewer

The mission viewer renders:

- RGB input
- depth input
- ground-truth semantic map
- predicted semantic map plus HUD

Run it with:

```bash
python docs/scripts/hesia_mission_viewer.py
```

Outputs:

- `docs/generated/hesia_mission_viewer.mp4`
- `docs/generated/hesia_mission_viewer_contact_sheet.jpg`
- `docs/generated/hesia_mission_viewer_summary.json`

## Current status

What is already proven:

- local mission simulator generation works
- mission supervised training works
- mission closed-loop smoke works
- Arkveld GPU smoke training works

What is not yet solved:

- expert mission policy is still imperfect
- closed-loop mission success is not yet at production grade
- this branch is not yet exported and validated on Jetson
