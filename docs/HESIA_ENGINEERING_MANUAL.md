# HESIA Engineering Manual

## 1. Purpose

HESIA is a hardened drone-control firmware and server stack built around:

- a C++ drone runtime for Jetson
- a C++ secure session server
- OP-TEE as the trusted execution anchor
- post-quantum cryptography already selected by the project
- an embedded perception stack currently based on YOLO + MiDaS, with sequential state hooks already present in the clean pipeline

This manual is the handover document for engineers who need to rebuild, deploy, maintain, audit, or extend the system without guessing hidden assumptions.

## 2. Repository layout

- `drone_source/`: drone runtime, transport, crypto glue, runtime hardening, video pipeline, OP-TEE client.
- `server_source/`: server runtime, secure-session orchestration, audit, policy loading, UI tooling.
- `drone_transition_source/`: Jetson transition assets, OP-TEE TA/host skeleton, validation and provisioning helpers.
- `security/`: policy profiles and deployment templates.
- `tools/`: build, validation, reproducibility, deployment and repository safety tooling.
- `docs/`: handover documentation.

## 3. High-level architecture

### 3.1 Runtime split

- Drone:
  - initiates mTLS to the server
  - performs a PQC-authenticated HESIA handshake after TLS
  - streams secure telemetry and encrypted JPEG video packets
  - uses OP-TEE for sealed storage, attestation, ML-DSA signing, session-auth gating, and rollback metadata
- Server:
  - terminates TLS 1.3 with mTLS
  - verifies the drone identity, firmware hash allowlist, TEE evidence, and session transcript binding
  - signs `SERVER_AUTH` with the server ML-DSA identity stored in the OP-TEE server slot
  - receives secure telemetry and encrypted video data
- TA / OP-TEE:
  - stores sealed secrets and ML-DSA key material
  - exports TEE-bound public anchors
  - performs ML-DSA signatures inside the TA
  - owns slot metadata for staged update bookkeeping
  - enforces session-auth provisioning before protected commands are allowed in production

### 3.2 Transport stack

The current validated transport sequence is:

1. TCP connect
2. TLS 1.3 handshake with mTLS
3. TLS exporter binding
4. HESIA message exchange:
   - `HELLO`
   - `HELLO_ACK`
   - `KEY_INIT`
   - `KEY_RESP`
   - `DRONE_AUTH`
   - `SERVER_AUTH`
   - `CONFIRM`
5. Secure session
6. Telemetry + video over authenticated channels

### 3.3 Cryptographic primitives in use

The project must keep the chosen primitive families.

Current stack in the validated Jetson profile:

- TLS 1.3 with mTLS
- ML-KEM-1024 for PQC key establishment inside the HESIA handshake
- ML-DSA-87 for identity and session signatures
- Ed25519 for signed policy verification root
- AES-256-GCM for secure message and video channels
- SHA3-512 / HKDF-SHA3-512 in the HESIA session path
- TEE-resident ML-DSA slots for drone and server identities

## 4. Drone runtime internals

### 4.1 Entry points

- `drone_source/main.cpp`
- `drone_source/hesia_drone.cpp`
- `drone_source/drone_network.cpp`

### 4.2 Core responsibilities

- load and verify the signed policy
- load certificates, pinned public keys, and sealed material from `secure_dir`
- verify OP-TEE session-auth readiness in production
- initialize runtime protection and sandboxing
- establish TLS and the HESIA session
- initialize the clean video pipeline
- send secure telemetry heartbeat and secure video packets

### 4.3 Video pipeline

Current production behavior on Jetson:

- `CleanPipeline` is the active path
- YOLO and MiDaS are both used
- processed frames are encrypted with `VideoChannel`
- the callback path sends frames to the secure transport queue

Important implementation note:

- the legacy `video_processing_loop()` path still exists in the codebase
- the active runtime path after handshake is `init_clean_pipeline()`
- the sequential `yolo_state` field already exists in `clean_pipeline.hpp`, which matters for the future multimodal replacement work

### 4.4 Runtime backpressure handling

The drone now avoids letting video saturate control traffic:

- `drone.video_send_queue_max` controls the queue depth
- `drone.video_min_send_interval_ms` limits enqueue cadence for video frames
- when the queue is under pressure, control messages can evict older `VIDEO_DATA` entries rather than losing telemetry
- video backpressure warnings are rate-limited to avoid log storms

Validated Jetson setting:

- `drone.video_send_queue_max=96`
- `drone.video_min_send_interval_ms=80`

This was chosen as the best current compromise between:

- keeping secure telemetry stable
- keeping video flowing
- avoiding log storms and queue churn
- not over-complicating the transport

## 5. Server runtime internals

### 5.1 Entry points

- `server_source/src/main.cpp`
- `server_source/src/hesia_server_session.cpp`

### 5.2 Core responsibilities

- listen on TCP/9000
- enforce mTLS
- bind the HESIA handshake to the TLS exporter
- load pinned drone ML-DSA and TEE public keys
- load the server ML-DSA identity from the OP-TEE `server` slot
- enforce allowlist checks and signed policy verification
- decrypt secure telemetry and video data
- write operational logs and UI artifacts

### 5.3 Session state

The session code explicitly logs the critical checkpoints:

- `KEY_EXCHANGE ok`
- `DRONE_AUTH payload verified`
- `SERVER_AUTH payload signed`
- `SERVER_AUTH frame sent`
- `CONFIRM received`
- `SECURE_SESSION established`

These markers are the fastest way to prove a complete session on target.

## 6. OP-TEE / TA design

### 6.1 TA command surface

Defined in `drone_transition_source/optee_ta_skeleton/ta/include/ta_hesia.h`.

Main commands:

- seal / unseal
- rotate or wipe the sealing key
- HKDF
- firmware version check/read/reset
- export attestation public key
- sign attestation digest
- set and recover session-auth secret
- stage and commit slot metadata
- import, export, sign, status, and generate ML-DSA key material

### 6.2 ML-DSA slots

Two explicit slots are in use:

- `HESIA_MLDSA_SLOT_DRONE`
- `HESIA_MLDSA_SLOT_SERVER`

Operational meaning:

- drone identity signing must come from the drone slot
- server `SERVER_AUTH` signing must come from the server slot
- production should fail closed if the required slot is unavailable

### 6.3 OP-TEE client API surface

Defined in `drone_source/optee_client.hpp`.

Key exported flows:

- `optee_require_session_auth_ready_or_throw()`
- `optee_import_mldsa_key_from_sealed_blob()`
- `optee_get_mldsa_public_key()`
- `optee_sign_mldsa_payload()`
- `optee_mldsa_signing_ready()`
- `optee_stage_slot_update()`
- `optee_commit_slot_boot()`
- `optee_read_slot_meta()`

### 6.4 Current production-relevant constraint

The validated Jetson profile does not use RPMB because the Jetson Orin Nano Super target currently boots from removable SD and exposes no RPMB capability.

That means the Jetson runtime profile intentionally sets:

- `drone.require_rpmb_rollback_storage=0`
- `drone.require_ab_slots=0`
- `drone.require_boot_measure=0`
- `drone.require_asset_manifest=0`

This is an explicit hardware-profile compromise, not a claim that these features are no longer desirable.

## 7. Policy model

### 7.1 Trust model

The policy is not trusted because it exists on disk.

The policy is trusted because:

- the Ed25519 verification root is embedded in the binaries
- the PQC policy verification root is embedded in the binaries
- runtime signatures are checked before policy contents are accepted

### 7.2 Jetson runtime profile

Source profile:

- `security/policies/jetson_orin_nano_super_runtime.policy.conf`

Important characteristics:

- production fuse enabled
- mTLS required
- TEE attestation required
- OP-TEE session authentication required
- ML-DSA signing in TEE required
- RPMB requirement disabled for this hardware profile
- TEE HKDF disabled for this specific profile because the Jetson OP-TEE runtime does not currently support the expected HKDF path

## 8. Secure storage and key material

### 8.1 Active runtime directories

- Policy:
  - `/etc/hesia/policy`
- Secure blobs and pinned anchors:
  - `/etc/hesia/secure`
- Certificates:
  - `/etc/hesia/certs`
- Release binaries:
  - `/opt/hesia/bin`
- Runtime libraries:
  - `/opt/hesia/lib`
- Debug sidecars:
  - `/var/lib/hesia/debug`
- Runtime logs:
  - `/var/log/hesia`

### 8.2 Important files in `/etc/hesia/secure`

- `optee_session_auth.sealed`
- `dilithium5_sk.sealed`
- `hesia_seed.sealed`
- `server_public.bin`
- `drone_public.bin`
- `drone_tee_attest_pub.bin`
- `firmware_allowlist.txt`

Expected permissions for sealed blobs:

- owner `root`
- group `hesia`
- mode `0640`
- immutable bit set where possible

### 8.3 Current server identity posture

Current validated posture:

- server ML-DSA signing is loaded from the OP-TEE `server` slot
- `/etc/hesia/secure/server_secret.bin` is not used as an active production runtime secret
- the active deployed server identity on Jetson is TEE-backed

## 9. Build and release

### 9.1 Drone build

Validated Jetson build configuration:

```bash
cmake -S /home/ajax/.cache/.hesia/src/drone \
  -B /home/ajax/.cache/.hesia/build/drone-clang-cfi \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DHESIA_ENABLE_CFI=ON \
  -DHESIA_DISABLE_CFI_OPENCV=ON \
  -DHESIA_STRIP_SYMBOLS=ON \
  -DHESIA_ALLOW_SOFT_SIGN=OFF \
  -DLIBOQS_ROOT_DIR=/home/ajax/.cache/.hesia/deps/liboqs/build
cmake --build /home/ajax/.cache/.hesia/build/drone-clang-cfi -j2
```

### 9.2 Server build

Validated Jetson build configuration:

```bash
cmake -S /home/ajax/.cache/.hesia/work/runtime_20260420/server_source \
  -B /home/ajax/.cache/.hesia/build/server-runtime-20260420-tee \
  -DCMAKE_BUILD_TYPE=Release \
  -DHESIA_ALLOW_SOFT_SIGN=OFF \
  -DLIBOQS_ROOT_DIR=/home/ajax/.cache/.hesia/deps/liboqs/install
cmake --build /home/ajax/.cache/.hesia/build/server-runtime-20260420-tee -j2
```

### 9.3 Hardening and reverse-cost choices

The chosen hardening/obfuscation compromise is intentionally conservative and realistic:

- stripped PIE executables
- split debug sidecars kept outside runtime path
- hidden symbols where possible
- RELRO / NOW / noexecstack
- stack protector and fortify
- section GC and link-time optimization where supported
- Clang CFI on the drone build
- immutable release binaries in `/opt/hesia/bin`
- immutable Sentinel runtime library in `/opt/hesia/lib`
- root-owned release path outside the mutable build tree
- compiler identification strings removed where supported
- aggressive but low-risk linker optimization for release artifacts

What was deliberately not used:

- packers
- runtime code virtualization
- fragile anti-debug tricks that damage Jetson stability
- opaque binary-only wrappers

Reason:

- the system is safety-relevant
- debugging, incident response, and deterministic rebuilds matter
- reverse cost must go up without making the platform unmaintainable

### 9.4 Release deployment

Release deployment helper:

- `tools/deploy_hesia_release.sh`
- `tools/build_hesia_cloaked_release.sh`
- `tools/measure_release_artifact.py`
- `tools/jetson_benchmark_m2b.sh`

Template systemd units:

- `security/systemd/hesia-drone.service`
- `security/systemd/hesia-drone.hardening.conf`
- `security/systemd/hesia-server.service`

## 10. Reproducibility and supply-chain controls

The repository includes:

- reproducible-build flags in CMake
- SBOM generation
- opaque binary checks
- dependency declaration checks
- a reproducibility verifier

Primary tooling:

- `tools/verify_reproducible_build.py`
- `tools/build_repro_targets.sh`
- `tools/build_hesia_cloaked_release.sh`
- `tools/measure_release_artifact.py`
- `tools/check_opaque_binaries.py`
- `tools/check_declared_dependencies.py`
- `tools/generate_firmware_sbom.py`

## 11. Jetson installation and provisioning sequence

Minimum order for a clean target:

1. Install system dependencies, OP-TEE client dependencies, TensorRT/OpenCV dependencies, and liboqs.
2. Provision `/etc/hesia/certs`, `/etc/hesia/policy`, and `/etc/hesia/secure`.
3. Provision sealed blobs and public anchors.
4. Import ML-DSA identities into TA slots.
5. Build drone and server release binaries.
6. Update `firmware_allowlist.txt` to match the exact deployed drone binary hash.
7. Deploy stripped release binaries into `/opt/hesia/bin` and the Sentinel shared library into `/opt/hesia/lib`.
8. Install systemd units.
9. Start `hesia-server.service`.
10. Start `hesia-drone.service`.
11. Run `hesia-validate.sh`.
12. Check session logs for a complete handshake.

## 12. Runtime validation checklist

Minimum proof that the platform is healthy:

- `hesia-server.service` is active
- `hesia-drone.service` is active
- `/proc/<drone_pid>/status` shows `NoNewPrivs: 1`
- `/proc/<drone_pid>/status` shows `Seccomp: 2`
- latest session log contains:
  - `KEY_EXCHANGE ok`
  - `DRONE_AUTH payload verified`
  - `SERVER_AUTH payload signed`
  - `CONFIRM received`
  - `SECURE_SESSION established`
  - `SECURE_MSG ok`
  - `VIDEO_DATA ok`

## 13. Maintenance

### 13.1 Key rotation

- Drone:
  - `drone_transition_source/scripts/rotate_drone_identity.sh`
- Server:
  - `server_source/tools/rotate_all_keys.sh`

### 13.2 Session-auth recovery

Relevant TA commands and host flows:

- `TA_HESIA_CMD_GET_RECOVERY_CHALLENGE`
- `TA_HESIA_CMD_RECOVER_SESSION_AUTH`

### 13.3 TA public anchor export

Use:

- `drone_transition_source/scripts/export_ta_public_anchors.sh`

### 13.4 OP-TEE session-auth provisioning

Use:

- `drone_transition_source/scripts/provision_optee_session_auth.sh`

## 14. Logging and forensic locations

- Drone and server logs:
  - `/var/log/hesia`
- Session logs:
  - `/var/log/hesia/drone/SERVERCPP.*.log`
- Optional UI output:
  - `/var/log/hesia/ui`
- Debug sidecars:
  - `/var/lib/hesia/debug`

## 15. Known constraints and explicit non-goals for the current Jetson profile

- No RPMB-backed rollback storage on this Jetson hardware profile.
- Boot measurement is intentionally disabled in the deployed Jetson runtime profile.
- Asset manifest enforcement is intentionally disabled in the deployed Jetson runtime profile.
- A/B slot enforcement is intentionally disabled in the deployed Jetson runtime profile.
- TEE HKDF is intentionally disabled in the deployed Jetson runtime profile because the expected OP-TEE runtime support is not currently available on this target.

These are not hidden gaps. They are explicit profile choices that must remain documented until the hardware and runtime stack change.

## 16. Next engineering priorities

1. Re-enable measured boot once the target platform and provisioning chain support it operationally.
2. Re-enable asset manifest verification for all model/config payloads once deployment packaging is stabilized.
3. Reintroduce A/B slot enforcement when the rollback store can be anchored credibly.
4. Extend the release process so the server unit is managed entirely through systemd from first boot.
5. Continue the ML replacement program for YOLO + sequential state logic while keeping MiDaS.

## 17. M2B deployment path

The current embedded multimodal candidate lives under:

- `ml/hesia_m2b`

The validated export and target benchmark path is:

1. export a monolithic ONNX artifact from the chosen checkpoint
2. copy it to the Jetson workspace
3. build a TensorRT FP16 engine on target
4. capture both inference-only and round-trip latency

Repository helpers:

- `ml/hesia_m2b/export_onnx.py`
- `ml/hesia_m2b/benchmark_tensorrt.py`
- `tools/jetson_benchmark_m2b.sh`
- `docs/HESIA_M2B_JETSON_BENCHMARK_2026-04-21.md`

Current validated Jetson result for the compact disturbed candidate:

- `avg_infer_ms=1.38`
- `avg_roundtrip_ms=2.36`
- `fps_equivalent=724.35`
