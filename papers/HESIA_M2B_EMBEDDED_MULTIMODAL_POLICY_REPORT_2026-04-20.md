# HESIA M2B Embedded Multimodal Policy Report

## Abstract

This report documents the current HESIA multimodal control policy track intended to replace the legacy YOLO plus sequence-model stack while keeping MiDaS as an external depth source. The main conclusion at this stage is practical: for the Jetson-first deployment target, a compact dense model found through a Lottery Ticket workflow and then fine-tuned in closed loop remains the best trade-off among speed, exportability, and policy competence.

## 1. Problem framing

HESIA needs an embedded policy architecture that:

- runs credibly on edge hardware
- remains exportable to ONNX and then to a Jetson deployment path
- does not depend on giant VLM stacks
- can absorb multimodal inputs from RGB, depth, compact state, and mission context
- preserves enough temporal reasoning to replace the previous Mamba-centered sequential logic

The target is not a language model.
The target is an embedded robot policy.
That constraint shapes all design decisions in this report.

## 2. Architecture decision

The current HESIA M2B family combines:

- a compact RGB visual encoder
- a compact depth encoder
- state and mission embeddings
- an export-friendly temporal mixer inspired by Mamba-2 style state-space duality
- BitNet-style low-cost linear control layers in the action head

This is a deliberate hybrid.

The temporal core is informed by Mamba-2 rather than copied verbatim from a heavy upstream kernel implementation. The reference motivation is from Dao and Gu, who describe the SSD framework and a Mamba-2 core layer that is faster while staying competitive with Transformers ([arXiv:2405.21060](https://arxiv.org/abs/2405.21060)). For the action head, HESIA borrows the deployment instinct of BitNet-style low-precision computation rather than trying to reproduce a full LLM quantization regime. The motivating reference is Ma et al. on BitNet b1.58 ([arXiv:2402.17764](https://arxiv.org/abs/2402.17764)).

Inference implication:

- Mamba-2 ideas provide the right bias for compact temporal processing
- BitNet-style low-cost layers are best used in the control head, not as a reason to turn the whole policy into an LLM-shaped artifact

## 3. Lottery Ticket position

HESIA does not treat sparse tickets as a deployment theater artifact.

The workflow is:

1. train a dense baseline
2. run iterative magnitude pruning / rewind
3. identify a robust winning ticket
4. derive a smaller dense deployment candidate
5. fine-tune that dense candidate in closed loop

This decision remains important because unstructured sparsity alone is not assumed to reduce Jetson latency. In HESIA, the ticket is a discovery mechanism for a smaller dense model family.

## 4. Simulator strategy

The executable simulator stack in the repository is intentionally staged.

Stage 0, already implemented:

- lightweight synthetic environment
- closed-loop rollouts
- disturbance curriculum
- DAgger-lite fine-tuning

Stage 1, recommended next:

- RotorPy, because it is Python-native and includes multirotor dynamics, wind models, actuator effects, and realistic sensors ([arXiv:2306.04485](https://arxiv.org/abs/2306.04485))
- Flightmare, because it remains a strong quadrotor simulation reference for perception and learning loops ([arXiv:2009.00563](https://arxiv.org/abs/2009.00563))

Stage 2, recommended sim-to-real stack:

- NVIDIA Isaac Sim, which NVIDIA describes as an open source reference framework for robotics simulation, testing, and synthetic data generation in physically based virtual environments ([NVIDIA Isaac Sim](https://developer.nvidia.com/isaac/sim))
- Pegasus Simulator, which is built on top of NVIDIA Omniverse and Isaac Sim and is explicitly designed for multirotor simulation with PX4 or custom Python control interfaces ([Pegasus Simulator docs](https://pegasussimulator.github.io/PegasusSimulator/))

The current HESIA position is practical:

- keep the lightweight environment for rapid architecture iteration
- do not pretend it is the final realism layer
- only move up the stack once the embedded policy family is stable enough to justify the operational cost

## 5. Real findings on Arkveld

Hardware used:

- host: Arkveld
- GPU: RTX 3070
- runtime: torch 2.11.0+cu128

### 5.1 Compact candidate

The strongest current candidate remains the compact dense model at:

- `494,387` parameters

Observed performance after closed-loop fine-tuning:

- `9.36 ms`
- `106.87 FPS`
- rollout success on the clean closed-loop synthetic environment: `95.8%`

### 5.2 Disturbance-aware curriculum

The simulator was extended with:

- persistent wind bias
- gust noise
- RGB and depth corruption
- state noise
- render jitter
- action latency

Starting from the already fine-tuned compact model:

- pre-curriculum success at disturbance scale `0.5`: `91.7%`
- post-curriculum success at disturbance scale `0.5`: `100%`
- post-curriculum speed: `9.59 ms`, `104.33 FPS`

This is one of the most important current findings.
Robustness improved without changing the deployed parameter count.

### 5.3 Scale-up sanity check

A materially larger family was also tested.

Recommended dense derivative:

- `1,528,923` parameters
- `13.94 ms`
- `71.75 FPS`

Conclusion:

- the larger family is feasible on Arkveld
- it is not yet justified for the embedded target
- the compact family still dominates the speed-to-competence trade-off

## 6. Jetson deployment proof

The compact disturbed candidate is no longer only a desktop result.

It has now been exported as a monolithic ONNX artifact and benchmarked on the Jetson target using TensorRT `10.3.0` through the Python API, which was necessary because the target image exposes:

- TensorRT Python bindings
- `numpy`
- but not `torch`
- not `onnxruntime`
- and not `trtexec`

Observed target result at batch `1` and FP16:

- engine build time: `128.07 s`
- inference-only latency: `1.38 ms`
- round-trip latency with host/device copies: `2.36 ms`
- implied throughput from inference-only timing: `724.35 FPS`

This matters because it changes the engineering conclusion.

The current HESIA M2B candidate is no longer merely exportable in principle.
It is now proven to map to a credible Jetson execution path with ample latency headroom.

## 7. RotorPy bridge result

The repository now also contains a genuine Stage 1 simulator bridge:

- RotorPy provides the multirotor dynamics backend
- the existing HESIA renderer still provides compact RGB/depth observations
- rollout and closed-loop training can now switch to `--backend rotorpy`

This is a useful compromise.
It adds more physical dynamics without forcing HESIA to wait for a full Isaac/Pegasus stack before doing harder transfer tests.

The first honest result is mixed:

- the bridge runs
- the compact disturbed candidate does not yet solve the RotorPy hybrid task
- the first raw bridge was too punitive because obstacle sampling could create immediate near-collision states
- after fixing scene sampling, the backend became healthier:
  - pre-fine-tuning average return improved to `-43.25`
  - post-smoke average return improved further to `-42.18`
  - expert-action gap dropped to about `0.19`
- success is still `0%`, so this is progress in trainability, not mission readiness

That means the problem is now better localized.

The bottleneck is not whether HESIA can execute a more realistic simulator.
The bottleneck is policy adaptation to that more physical regime.

## 8. Engineering decision

For the current HESIA roadmap, the right next deployment candidate is:

- the compact dense closed-loop model
- with disturbance-aware fine-tuning
- exported to ONNX
- kept aligned with a future Jetson TensorRT path

This is the path with the strongest evidence today.

## 9. What remains

The following work is still needed before claiming a full replacement of the legacy perception-control stack:

- improve transfer from the lightweight simulator to the RotorPy hybrid backend
- increase simulator realism with a RotorPy or Flightmare bridge
- then validate the same compact family on an Isaac Sim or Pegasus-based path
- connect the policy outputs to a higher-fidelity actuator loop instead of only the current synthetic action interface

## 10. Bottom line

The main hypothesis has survived first contact with real experiments:

- Mamba-2-inspired temporal structure is useful in an embedded robot policy
- BitNet-style low-cost control heads fit the deployment objective better than a transformer-heavy design
- Lottery Ticket pruning is valuable as a model-family search tool
- closed-loop fine-tuning matters more than offline proxy loss alone
- a compact dense policy remains the best current HESIA deployment candidate
