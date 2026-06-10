# HESIA Installation And Provisioning Guide

## Scope

This guide is the practical installation path for bringing up HESIA on a clean Linux target.

It is written for:

- Jetson Orin Nano Super operators
- firmware engineers rebuilding the drone runtime
- platform engineers provisioning OP-TEE and secure material
- operations teams preparing a release target from scratch

This document covers only workflows that are already represented in the repository.

## 1. Target prerequisites

Minimum target assumptions:

- Linux with `systemd`
- OpenSSL, CMake, Clang or GCC, and core build tooling
- OP-TEE client stack available on the target
- OpenCV and TensorRT available for the drone build
- `liboqs` installed and discoverable
- runtime group `hesia` created
- writable deployment paths:
- `/etc/hesia`
- `/opt/hesia/bin`
- `/opt/hesia/lib`
- `/var/log/hesia`
- `/var/lib/hesia/debug`

Recommended runtime users/groups:

- service user: `root`
- runtime group: `hesia`
- supplementary groups for the drone service:
  - `video`
  - `render`
  - `tee`
  - `hesia`

## 2. Repository-side inputs

Required repository areas:

- [drone_source](/C:/Users/matis/Documents/Hesia-Firmware/drone_source)
- [server_source](/C:/Users/matis/Documents/Hesia-Firmware/server_source)
- [drone_transition_source](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source)
- [security](/C:/Users/matis/Documents/Hesia-Firmware/security)
- [tools](/C:/Users/matis/Documents/Hesia-Firmware/tools)

Primary policy source for Jetson:

- [jetson_orin_nano_super_runtime.policy.conf](/C:/Users/matis/Documents/Hesia-Firmware/security/policies/jetson_orin_nano_super_runtime.policy.conf)

## 3. Target directory layout

Create and own the runtime tree:

```bash
install -d -o root -g hesia -m 0750 /etc/hesia
install -d -o root -g hesia -m 0750 /etc/hesia/policy
install -d -o root -g hesia -m 0750 /etc/hesia/secure
install -d -o root -g hesia -m 0750 /etc/hesia/certs
install -d -o root -g hesia -m 0750 /etc/hesia/keys
install -d -o root -g hesia -m 0750 /var/log/hesia
install -d -o root -g hesia -m 0750 /var/lib/hesia/debug
install -d -o root -g hesia -m 0750 /opt/hesia/bin
install -d -o root -g hesia -m 0750 /opt/hesia/lib
```

## 4. Build dependencies

Drone build expects:

- OpenCV headers and libraries
- TensorRT headers and libraries
- OP-TEE client headers and `libteec`
- `libseccomp`
- `liboqs`
- GNAT / `gprbuild` when Sentinel is enabled

Server build expects:

- OpenSSL
- OP-TEE client headers and `libteec`
- `libseccomp`
- `liboqs`

## 5. Build procedures

### 5.1 Drone release build

Validated Jetson build shape:

```bash
cmake -S /home/ajax/.cache/.hesia/src/drone \
  -B /home/ajax/.cache/.hesia/build/drone-clang-cfi \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DHESIA_ENABLE_CFI=ON \
  -DHESIA_DISABLE_CFI_OPENCV=ON \
  -DHESIA_ENABLE_RELEASE_CLOAKING=ON \
  -DHESIA_STRIP_SYMBOLS=ON \
  -DHESIA_ALLOW_SOFT_SIGN=OFF \
  -DLIBOQS_ROOT_DIR=/home/ajax/.cache/.hesia/deps/liboqs/build
cmake --build /home/ajax/.cache/.hesia/build/drone-clang-cfi -j2
```

### 5.2 Server release build

```bash
cmake -S /home/ajax/.cache/.hesia/work/runtime_20260420/server_source \
  -B /home/ajax/.cache/.hesia/build/server-runtime-20260420-tee \
  -DCMAKE_BUILD_TYPE=Release \
  -DHESIA_ENABLE_RELEASE_CLOAKING=ON \
  -DHESIA_STRIP_SYMBOLS=ON \
  -DHESIA_ALLOW_SOFT_SIGN=OFF \
  -DLIBOQS_ROOT_DIR=/home/ajax/.cache/.hesia/deps/liboqs/install
cmake --build /home/ajax/.cache/.hesia/build/server-runtime-20260420-tee -j2
```

## 6. Certificates and pinned material

Deploy:

- policy files into `/etc/hesia/policy`
- runtime certificates into `/etc/hesia/certs`
- runtime public anchors and sealed blobs into `/etc/hesia/secure`

Key runtime files normally expected in `/etc/hesia/secure`:

- `optee_session_auth.sealed`
- `dilithium5_sk.sealed`
- `hesia_seed.sealed`
- `server_public.bin`
- `drone_public.bin`
- `drone_tee_attest_pub.bin`
- `firmware_allowlist.txt`

Expected permissions:

- owner `root`
- group `hesia`
- mode `0640`
- immutable bit enabled outside maintenance windows when possible

## 7. OP-TEE bootstrap

### 7.1 Provision session authentication

Use:

```bash
HESIA_TA_HOST_TOOL=/path/to/host_tool \
bash /path/to/provision_optee_session_auth.sh /etc/hesia/secure
```

Script reference:

- [provision_optee_session_auth.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/provision_optee_session_auth.sh)

### 7.2 Export public anchors from the TA

Use:

```bash
HESIA_TA_HOST_TOOL=/path/to/host_tool \
HESIA_RUNTIME_GROUP=hesia \
bash /path/to/export_ta_public_anchors.sh /etc/hesia/secure /etc/hesia/keys
```

Outputs installed by the script:

- `dilithium5_pk.bin`
- `drone_tee_attest_pub.bin`
- `tee_attest_mldsa_pub.bin`
- `/etc/hesia/keys/drone_public.bin`

Script reference:

- [export_ta_public_anchors.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/export_ta_public_anchors.sh)

### 7.3 Export the server slot public anchor

If the server is configured to sign from the OP-TEE `server` slot, the pinned server public key used by the drone must be refreshed from that slot as well.

Use:

```bash
HESIA_TA_HOST_TOOL=/path/to/host_tool \
bash /path/to/export_server_slot_public_anchor.sh /etc/hesia/secure
```

Output installed by the script:

- `/etc/hesia/secure/server_public.bin`

Script reference:

- [export_server_slot_public_anchor.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/export_server_slot_public_anchor.sh)

### 7.4 Rotate the drone identity

Use:

```bash
HESIA_TA_HOST_TOOL=/path/to/host_tool \
bash /path/to/rotate_drone_identity.sh /etc/hesia/secure
```

Script reference:

- [rotate_drone_identity.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/rotate_drone_identity.sh)

## 8. Policy deployment

Deploy the signed policy set into `/etc/hesia/policy`:

- `policy.conf`
- `policy.sig`
- `policy.sig.pqc`

For the current Jetson SD profile, the active policy intentionally disables:

- RPMB-backed rollback enforcement
- strict A/B slot enforcement
- boot-measure enforcement
- asset-manifest enforcement

This is a hardware-profile decision, not a general security recommendation.

## 9. Release deployment

Deploy only stripped release binaries into `/opt/hesia/bin`, and deploy the Sentinel runtime library into `/opt/hesia/lib`.

Recommended release steps:

```bash
install -m 0750 -o root -g hesia /path/to/hesia_drone /opt/hesia/bin/hesia_drone
install -m 0750 -o root -g hesia /path/to/hesia_server_cpp /opt/hesia/bin/hesia_server_cpp
install -m 0750 -o root -g hesia /path/to/libhesia_sentinel.so /opt/hesia/lib/libhesia_sentinel.so
chattr +i /opt/hesia/bin/hesia_drone 2>/dev/null || true
chattr +i /opt/hesia/bin/hesia_server_cpp 2>/dev/null || true
chattr +i /opt/hesia/lib/libhesia_sentinel.so 2>/dev/null || true
```

Keep debug sidecars outside runtime execution paths.

Reusable helper tooling:

- `tools/build_hesia_cloaked_release.sh`
- `tools/measure_release_artifact.py`
- `tools/jetson_benchmark_m2b.sh`

## 10. Firmware allowlist refresh

Every deployed drone binary change must be reflected in `/etc/hesia/secure/firmware_allowlist.txt`.

```bash
FW_HASH=$(openssl dgst -sha3-512 /opt/hesia/bin/hesia_drone | awk '{print $2}')
chattr -i /etc/hesia/secure/firmware_allowlist.txt 2>/dev/null || true
printf 'sha3-512:%s\n' "$FW_HASH" > /etc/hesia/secure/firmware_allowlist.txt
openssl pkeyutl -sign -rawin \
  -inkey /etc/hesia/secure/allowlist_signing.key \
  -in /etc/hesia/secure/firmware_allowlist.txt \
  | base64 -w0 > /etc/hesia/secure/firmware_allowlist.txt.sig
printf '\n' >> /etc/hesia/secure/firmware_allowlist.txt.sig
chown root:hesia /etc/hesia/secure/firmware_allowlist.txt /etc/hesia/secure/firmware_allowlist.txt.sig
chmod 0640 /etc/hesia/secure/firmware_allowlist.txt /etc/hesia/secure/firmware_allowlist.txt.sig
chattr +i /etc/hesia/secure/firmware_allowlist.txt 2>/dev/null || true
chattr +i /etc/hesia/secure/firmware_allowlist.txt.sig 2>/dev/null || true
```

The server verifies the detached Ed25519 signature at `/etc/hesia/secure/firmware_allowlist.txt.sig`.
Do not write `/etc/hesia/secure/firmware_allowlist.sig`; that legacy-looking name is not consumed by the runtime.

## 11. systemd installation

Service templates:

- [hesia-drone.service](/C:/Users/matis/Documents/Hesia-Firmware/security/systemd/hesia-drone.service)
- [hesia-drone.hardening.conf](/C:/Users/matis/Documents/Hesia-Firmware/security/systemd/hesia-drone.hardening.conf)
- [hesia-server.service](/C:/Users/matis/Documents/Hesia-Firmware/security/systemd/hesia-server.service)

Install and reload:

```bash
install -m 0644 /path/to/hesia-drone.service /etc/systemd/system/hesia-drone.service
install -d -m 0755 /etc/systemd/system/hesia-drone.service.d
install -m 0644 /path/to/hesia-drone.hardening.conf /etc/systemd/system/hesia-drone.service.d/hardening.conf
install -m 0644 /path/to/hesia-server.service /etc/systemd/system/hesia-server.service
systemctl daemon-reload
systemctl enable hesia-server.service hesia-drone.service
```

## 12. First startup

Preferred order:

```bash
systemctl start hesia-server.service
systemctl start hesia-drone.service
```

Then verify:

- `systemctl status hesia-server.service`
- `systemctl status hesia-drone.service`
- `Seccomp: 2`
- `NoNewPrivs: 1`
- latest session log under `/var/log/hesia/drone/`

## 13. Mandatory post-install validation

Run:

```bash
bash /path/to/hesia-validate.sh
```

Expected proof:

- policy signatures validate
- secure files have correct permissions
- sandbox runtime is active
- services are alive
- latest session log shows:
  - `KEY_EXCHANGE ok`
  - `DRONE_AUTH`
  - `SERVER_AUTH`
  - `SECURE_SESSION established`
  - telemetry and `VIDEO_DATA ok`

## 14. Known Jetson-specific constraints

Current hardware profile facts:

- removable SD boot
- no RPMB capability
- profile explicitly tuned around that fact

Do not silently turn RPMB back on in the policy unless the boot medium changes and the platform actually exposes it.

## 15. Optional but recommended M2B validation

If the multimodal candidate is part of the deployment package, validate the embedded inference path on Jetson after the core firmware is healthy.

From the repository root:

```bash
./tools/jetson_benchmark_m2b.sh
```

Expected artifact:

- `artifacts/jetson_ml/hesia_m2b_tensorrt_fp16.json`

Validated current target profile for this workflow:

- TensorRT Python bindings present
- `numpy` present
- `torch` absent
- `onnxruntime` absent
- `trtexec` absent

That is why the benchmark helper builds and executes the engine through the TensorRT Python API rather than depending on `trtexec`.
