# HESIA Jetson Baseline - 2026-04-20

## Scope

This document records the baseline that was actually validated on the Jetson Orin Nano Super target on 2026-04-20.

It is not a target-state dream sheet.
It is the evidence-backed state that ran on hardware.

## Hardware and platform

- Target: Jetson Orin Nano Super
- Access path: Tailscale / SSH
- Boot medium: removable SD
- Consequence: no RPMB capability available on this platform profile

## Deployed runtime paths

- Drone binary:
  - `/opt/hesia/bin/hesia_drone`
- Server binary:
  - `/opt/hesia/bin/hesia_server_cpp`
- Debug sidecars:
  - `/var/lib/hesia/debug/hesia_drone.debug`
  - `/var/lib/hesia/debug/hesia_server_cpp.debug`
- Policy:
  - `/etc/hesia/policy/policy.conf`
- Secure material:
  - `/etc/hesia/secure`

## Binary hardening state

Validated on target:

- stripped PIE executables
- root-owned deployment
- group `hesia`
- mode `0750`
- immutable bit set on deployed release binaries

Observed target state:

- `/opt/hesia/bin/hesia_drone` size: `1016552`
- `/opt/hesia/bin/hesia_server_cpp` size: `448912`
- `/opt/hesia/bin/hesia_drone` sha256: `9c565aea4555ffe3e503ca2557ce190c1c31dce2b4c2476b5cd8afd466f60cc3`
- `/opt/hesia/bin/hesia_server_cpp` sha256: `3d4d27e4e7012b6db7677cc2aa7a3e916504059a5d6fbc0d4d54c7508877237d`
- `/var/lib/hesia/debug/hesia_drone.debug` size: `178200`
- `/var/lib/hesia/debug/hesia_server_cpp.debug` size: `102904`

## systemd state

Installed units:

- `hesia-drone.service`
- `hesia-server.service`

Validated properties:

- `ExecStart=/opt/hesia/bin/hesia_drone`
- `ExecStart=/opt/hesia/bin/hesia_server_cpp`
- `NoNewPrivileges=yes`
- `PrivateTmp=yes`
- hardening drop-in loaded for the drone service

Runtime proof:

- `/proc/<drone_pid>/status` showed `NoNewPrivs: 1`
- `/proc/<drone_pid>/status` showed `Seccomp: 2`

## Policy profile in use

Source profile:

- `security/policies/jetson_orin_nano_super_runtime.policy.conf`

Jetson-specific enabled controls:

- mTLS required
- TEE attestation required
- OP-TEE session authentication required
- ML-DSA signing in TEE required

Jetson-specific disabled controls for this profile:

- `require_boot_measure=0`
- `require_asset_manifest=0`
- `require_ab_slots=0`
- `require_rpmb_rollback_storage=0`

Additional runtime tuning validated:

- `drone.video_send_queue_max=96`
- `drone.video_min_send_interval_ms=80`

## Validation results

### 1. Session proof

Validated session log:

- `/var/log/hesia/drone/SERVERCPP.100.101.152.53:41530.log`

Observed milestones:

- `KEY_EXCHANGE ok`
- `Waiting for DRONE_AUTH`
- `DRONE_AUTH frame received`
- `DRONE_AUTH payload verified`
- `SERVER_AUTH payload signed`
- `SERVER_AUTH frame sent`
- `Initializing SecureChannel`
- `SecureChannel ready`
- `VideoChannel ready`
- `CONFIRM received (sig_len=4627)`
- `SECURE_SESSION established`
- `SECURE_MSG ok`
- `CONST telemetry update ok`
- `VIDEO_DATA ok`

### 2. Sandboxing proof

Validated process status:

- `NoNewPrivs: 1`
- `Seccomp: 2`

Validated service hardening:

- `ProtectSystem=full`
- `ProtectHome=read-only`
- `PrivateTmp=yes`
- `LockPersonality=yes`
- `RestrictNamespaces=yes`
- `ProtectProc=invisible`

### 3. Validation script result

`hesia-validate.sh` result on target:

- Pass: `28`
- Warn: `5`
- Fail: `0`

Warnings were expected for:

- no boot measure in this Jetson profile
- no separate asset manifest in this Jetson profile
- no A/B slot enforcement in this Jetson profile
- no RPMB-backed rollback storage in this Jetson profile
- removable SD media

### 4. Policy crypto verification

Validated on target:

- Ed25519 policy signature verified
- PQC policy signature verified (`ML-DSA-87`)

### 5. M2B deployment benchmark

Validated multimodal candidate benchmark artifact:

- local JSON proof: `artifacts/jetson_ml/hesia_m2b_tensorrt_fp16.json`

Observed Jetson TensorRT result:

- `TensorRT 10.3.0`
- `FP16`
- `batch=1`
- `avg_infer_ms=1.38`
- `avg_roundtrip_ms=2.36`
- `fps_equivalent=724.35`

Operational note:

- the Jetson image has `tensorrt` Python bindings
- it does not have `torch`, `onnxruntime`, or `trtexec`
- the validated benchmark path therefore uses `ml/hesia_m2b/benchmark_tensorrt.py` plus `libcudart.so`

## Runtime observations

### Good

- drone and server both run from `/opt/hesia/bin`
- server signs from the OP-TEE `server` slot
- `/etc/hesia/secure/server_public.bin` is aligned with the exported OP-TEE `server` slot public key
- drone uses TEE-backed ML-DSA signing
- secure telemetry and video flow
- previous stale allowlist issue was eliminated by re-hashing the deployed binary after rebuild
- previous `KEY_CONFIRM: signature serveur invalide` failure was eliminated by re-exporting the server slot public anchor
- previous `SecureChannel` constructor crash path was eliminated by making the runtime integrity monitor idempotent
- the cloaked release was validated with live telemetry and video after refreshing `/etc/hesia/secure/firmware_allowlist.txt` to the exact deployed hash

### Important profile caveats

- this target does not provide RPMB
- the current runtime profile is intentionally a Jetson deployment profile, not the full aspirational high-assurance profile

### Runtime tuning effect

The queue/backpressure changes eliminated the earlier repeated `queue full` noise during the validated run while preserving secure telemetry and video delivery.

## Rebuild inputs used for the validated release

Drone build directory:

- `/home/ajax/.cache/.hesia/build/drone-cloaked-cfi-20260420`

Server build directory:

- `/home/ajax/.cache/.hesia/build/server-cloaked-tee-20260420`

Important runtime note:

- the validated cloaked release was built with `HESIA_ALLOW_SOFT_SIGN=OFF`
- during the validated session, the server used the OP-TEE `server` slot at runtime

Release deploy helper:

- `tools/deploy_hesia_release.sh`
- `tools/build_hesia_cloaked_release.sh`
- `tools/measure_release_artifact.py`
- `tools/jetson_benchmark_m2b.sh`

## What must be repeated after any new release

1. rebuild binaries
2. redeploy to `/opt/hesia/bin`
3. refresh `firmware_allowlist.txt` with the exact deployed drone hash
4. restart both services
5. run `hesia-validate.sh`
6. confirm a new session log shows a full handshake and live traffic
