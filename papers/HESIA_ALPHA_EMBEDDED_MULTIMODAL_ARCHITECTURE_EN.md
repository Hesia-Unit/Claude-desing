# HESIA Alpha: A Compact Ternary State-Space Multimodal Architecture for Embedded Autonomy

Date: 2026-05-09

## Abstract

HESIA Alpha proposes a compact multimodal architecture for embedded autonomy that combines three efficiency mechanisms: BitNet-style ternary linear projections, Mamba-2-inspired state-space temporal modeling, and lottery-ticket pruning. The model is designed for a practical hardware envelope: RTX 3070 training experiments, later Jetson Orin Nano deployment, limited memory, and real-time inference constraints. The current implementation is a bounded research prototype, not a final trained checkpoint. This phase converts the main linear decision and temporal projections to `BitLinear`, adds an inference entry point, and defines Hugging Face sources for DINOv3 teacher distillation and UAV data.

## Motivation

Small embedded autonomy models need vision, state estimation, and temporal reasoning without the memory cost of large transformers. The Alpha design keeps the deployed model compact and uses large models only as training teachers. DINOv3 provides a modern visual representation target, Mamba-2 motivates linear-time temporal recurrence, and BitNet b1.58 motivates ternary projections that reduce weight storage.

## Related Work

DINOv3 is used as a visual teacher rather than a deployed backbone. Hugging Face lists DINOv3 image-feature-extraction models such as `facebook/dinov3-vits16-pretrain-lvd1689m`, `facebook/dinov3-vitb16-pretrain-lvd1689m`, and `facebook/dinov3-convnext-tiny-pretrain-lvd1689m`.

Mamba-2 is based on structured state-space duality. The paper "Transformers are SSMs" reports that the Mamba-2 core layer is a refinement of selective SSMs and can be 2 to 8 times faster while keeping competitive language modeling quality.

BitNet b1.58 represents weights with ternary values `{-1, 0, 1}`, commonly described as 1.58-bit average storage. Alpha applies this idea to linear projections through a straight-through-estimator `BitLinear` layer.

The Lottery Ticket Hypothesis motivates iterative magnitude pruning: train a dense model, identify a sparse subnetwork, rewind, and retrain the surviving weights.

## Architecture

The implemented Alpha candidate builds on the existing HESIA M2B model:

- RGB and depth streams are encoded by compact convolutional encoders.
- Visual features are fused and pooled into a token.
- Vehicle state and mission embedding are added to the visual token.
- A stack of selective SSM blocks processes the temporal sequence in linear time.
- Heads emit action chunks, risk score, and heatmap logits.

The mission variant adds semantic logits, instance masks, instance class logits, and stage logits.

In this phase, the principal linear projections in the base and mission models were converted to `BitLinear`: SSM input, delta, B, C, D, output projections, visual/state projections, risk head, stage head, and instance class head. Convolutional layers remain dense for now because convolution quantization and TensorRT calibration are a separate deployment step.

## DINOv3 Distillation Plan

Alpha does not deploy DINOv3 directly. The plan is:

1. Load a DINOv3 teacher on RTX 3070 for offline feature extraction.
2. Train the compact visual encoder to match teacher global and dense features.
3. Mix the distillation loss with mission/action/risk losses.
4. Export only the compact student to ONNX/TensorRT.

This preserves the benefit of foundation-level visual features while respecting embedded memory constraints.

## Training Data

The initial Hugging Face data manifest includes:

- `Meehai/dronescapes`
- `Meehai/dronescapes2`
- `Voxel51/dronescapes2_annotated_train_set`
- `pathikg/drone-detection-dataset`
- `lgrzybowski/seraphim-drone-detection-dataset`

These datasets cover UAV video, multimodal drone scenes, segmentation sanity checks, and drone/object detection. Each dataset must pass license review before publishing derived checkpoints.

## Inference and Pruning

The new inference script is:

```bash
python -m ml.hesia_m2b.infer --checkpoint artifacts/hesia_m2b_closed_loop.pt --out artifacts/hesia_m2b_infer.json
```

The existing training flow already supports lottery-ticket pruning through masks, global pruning by fraction, rewind state capture, and masked checkpoint export. The next benchmark must compare dense and pruned checkpoints with identical input shape, sequence length, batch size, and RTX 3070 environment.

## Current Evidence

Implemented files:

- `ml/hesia_m2b/bitlinear.py`
- `ml/hesia_m2b/model.py`
- `ml/hesia_m2b/mission_model.py`
- `ml/hesia_m2b/infer.py`
- `ml/hesia_m2b/hf_alpha_manifest.py`

Verification performed:

```bash
python -m py_compile ml\hesia_m2b\bitlinear.py ml\hesia_m2b\model.py ml\hesia_m2b\mission_model.py ml\hesia_m2b\hf_alpha_manifest.py ml\hesia_m2b\infer.py
python ml\hesia_m2b\hf_alpha_manifest.py
```

Torch is not installed in the current base Python environment, so no training or runtime benchmark is claimed in this phase.

## Limitations

The SSM block is Mamba-2-inspired but not an upstream Mamba-2 kernel. The DINOv3 path is a distillation plan and source manifest, not yet a completed teacher-student training run. Verbal performance comparable to market models is not claimed; it requires a language head, evaluation tasks, and real benchmarks. Jetson deployment remains a follow-up after CUDA/PyTorch and TensorRT validation.

## References

- DINOv3 paper page and model collection: https://huggingface.co/papers/2508.10104
- DINOv3 ViT-S/16 teacher: https://hf.co/facebook/dinov3-vits16-pretrain-lvd1689m
- DINOv3 ConvNeXt-Tiny teacher: https://hf.co/facebook/dinov3-convnext-tiny-pretrain-lvd1689m
- Mamba-2 / State Space Duality: https://arxiv.org/abs/2405.21060
- BitNet b1.58: https://arxiv.org/abs/2402.17764
- Lottery Ticket Hypothesis: https://huggingface.co/papers/1803.03635

## Addendum: Executed Alpha Run

After the initial architecture paper, Alpha was trained and benchmarked on the local RTX 3070.

The executable checkpoint is `artifacts/alpha/hesia_alpha_distilled.pt`. It contains 706,067 parameters, 31 `BitLinear` layers, zero dense `nn.Linear` layers, and a 32.5% lottery-ticket sparsity mask. The checkpoint is 3.62 MB and the verified ONNX export is 2.99 MB.

Synthetic CUDA training with ticket pruning reduced the final ticket loss to 0.0254. Hugging Face sampling collected 192 drone images from `Voxel51/dronescapes2_annotated_train_set` and `pathikg/drone-detection-dataset`. Direct DINOv3 loading from `facebook/dinov3-vits16-pretrain-lvd1689m` was attempted, but Hugging Face returned a gated-repository 401 error because the local environment is not authenticated for that model. The executable fallback teacher was `facebook/dinov2-small`.

Vision distillation reached 0.4451 cosine similarity on the held-out split after four epochs. On the benchmark subset, HESIA full forward latency was 11.12 ms versus 14.81 ms for DINOv2-small teacher feature extraction. The temporal core is a portable O(n) selective SSM, but it is still slower than a small optimized PyTorch transformer microbenchmark, so the Mamba-2 speed target is not claimed.

The size and BitLinear objectives are met for the prototype. DINOv3-level vision and market-comparable verbal behavior are not claimed yet. The former requires authenticated DINOv3 access; the latter requires a trained language decoder or compact LLM adapter.
