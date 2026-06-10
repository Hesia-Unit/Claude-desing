# HESIA M2B Jetson Benchmark - 2026-04-21

## Purpose

This note documents the Jetson-side benchmark path for the current compact multimodal candidate.

The target artifact is:

- `artifacts/arkveld_ml/hesia_m2b_closed_loop_arkveld_disturbed.onnx`

The current benchmark path is intentionally realistic for the validated Jetson target:

- no PyTorch on target
- no ONNX Runtime on target
- TensorRT Python bindings available on target

That is why the benchmark path uses:

- `ml/hesia_m2b/benchmark_tensorrt.py`
- `tools/jetson_benchmark_m2b.sh`

## Remote assumptions

Validated target observations before introducing this path:

- `python3` present on Jetson
- `numpy` present on Jetson
- `tensorrt` Python module present on Jetson (`10.3.0`)
- `torch` absent on Jetson
- `onnxruntime` absent on Jetson
- `trtexec` absent on Jetson

Because `trtexec` is missing, the benchmark path builds and executes the engine through the TensorRT Python API and `libcudart.so`.

## What the benchmark does

1. copies the current ONNX model to the Jetson workspace
2. builds a TensorRT engine on target
3. runs FP16 inference warmup
4. measures:
   - engine build time
   - inference-only time
   - round-trip time with host/device copies
5. writes a JSON artifact locally under:
   - `artifacts/jetson_ml/hesia_m2b_tensorrt_fp16.json`

## Invocation

From the repository root:

```bash
./tools/jetson_benchmark_m2b.sh
```

Or with a different ONNX artifact:

```bash
./tools/jetson_benchmark_m2b.sh /absolute/path/to/model.onnx
```

## Validated result

Measured on the current Jetson target:

- artifact: `artifacts/arkveld_ml/hesia_m2b_closed_loop_arkveld_disturbed.onnx`
- TensorRT: `10.3.0`
- precision: `FP16`
- batch size: `1`
- warmup: `20`
- measured iterations: `120`

Observed result:

- engine build time: `128074.72 ms`
- inference-only time: `1.38 ms`
- round-trip time with host/device copies: `2.36 ms`
- equivalent throughput from inference-only timing: `724.35 FPS`

Bound tensors:

- `rgb_seq`: `[1, 6, 3, 96, 160]`
- `depth_seq`: `[1, 6, 1, 96, 160]`
- `state_seq`: `[1, 6, 12]`
- `mission_ids`: `[1]`
- `heatmaps`: `[1, 2, 12, 20]`
- `action`: `[1, 8, 4]`
- `risk`: `[1, 1]`

Result artifact:

- `artifacts/jetson_ml/hesia_m2b_tensorrt_fp16.json`

## Packaging note

The ONNX artifact used for the validated benchmark was re-exported as a monolithic file.

Reason:

- the earlier artifact referenced an external weights sidecar
- the Jetson parser correctly failed when that hidden dependency was not present
- `ml/hesia_m2b/export_onnx.py` now supports explicit control of external-data export and defaults to the monolithic path for this deployment track

## Status

The path is now both implemented and validated on the current Jetson profile.

Any future Jetson image that adds `trtexec` can keep this path as a fallback, but should also add a `trtexec` comparison run because that better matches common field tooling.
