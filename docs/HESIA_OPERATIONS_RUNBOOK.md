# HESIA Operations Runbook

## 1. Daily checks

- `systemctl status hesia-server.service`
- `systemctl status hesia-drone.service`
- confirm the server listens on TCP/9000
- inspect the latest session log under `/var/log/hesia/drone/`
- confirm the drone process still shows:
  - `NoNewPrivs: 1`
  - `Seccomp: 2`

## 2. Startup sequence

Preferred order:

1. `systemctl start hesia-server.service`
2. `systemctl start hesia-drone.service`
3. watch the latest session log

Success markers:

- `SERVER_AUTH payload signed`
- `CONFIRM received`
- `SECURE_SESSION established`
- telemetry updates
- `VIDEO_DATA ok`

## 3. Shutdown sequence

Preferred order:

1. `systemctl stop hesia-drone.service`
2. `systemctl stop hesia-server.service`

This avoids stale connection noise and leaves the server ready for maintenance.

## 4. Runtime verification commands

Check services:

```bash
systemctl --no-pager --full status hesia-server.service
systemctl --no-pager --full status hesia-drone.service
```

Check process sandbox:

```bash
pid=$(systemctl show -p MainPID --value hesia-drone.service)
egrep '^(Name|NoNewPrivs|Seccomp):' "/proc/${pid}/status"
```

Check latest session:

```bash
f=$(ls -1t /var/log/hesia/drone/SERVERCPP.*.log | head -n 1)
tail -n 120 "$f"
```

Run the validator:

```bash
bash /home/ajax/.cache/.hesia/tmp/hesia-validate.sh
```

## 5. Firmware allowlist update after a drone rebuild

Whenever the deployed drone binary changes, the allowlist must be updated to the exact binary hash.

```bash
sudo tools/refresh_firmware_allowlist.sh /opt/hesia/bin/hesia_drone
```

If this step is missed, the server will reject the drone with:

- `Session error: Firmware hash not in allowlist`

## 6. Policy update procedure

1. Edit the source policy profile in `security/policies/`.
2. Re-sign the Ed25519 signature with the correct private key.
3. Re-sign the PQC signature with the correct ML-DSA private key.
4. Deploy:
   - `policy.conf`
   - `policy.sig`
   - `policy.sig.pqc`
   - optional public-key material for validation
5. Restart services.
6. Run the validator and confirm the session still establishes.

## 7. TA and OP-TEE maintenance

### 7.1 Export public anchors

Use:

```bash
drone_transition_source/scripts/export_ta_public_anchors.sh
```

### 7.2 Provision OP-TEE session auth

Use:

```bash
drone_transition_source/scripts/provision_optee_session_auth.sh
```

### 7.3 Rotate drone identity

Use:

```bash
drone_transition_source/scripts/rotate_drone_identity.sh
```

### 7.4 Rotate all server-side keys

Use:

```bash
server_source/tools/rotate_all_keys.sh
```

## 8. Troubleshooting

### 8.1 Drone cannot connect

Check:

- server service active
- policy host/port
- certificate paths
- pinned public keys in `/etc/hesia/secure`
- `firmware_allowlist.txt` hash matches the deployed drone binary

### 8.2 OP-TEE failure at startup

Check:

- `optee_session_auth.sealed` exists and is readable
- sealed blobs are `root:hesia 0640`
- immutable bits are not preventing a required maintenance write
- TA public anchors are exported and consistent

### 8.3 Session establishes but video stalls

Check:

- latest session log still shows `VIDEO_DATA ok`
- current policy values:
  - `drone.video_send_queue_max`
  - `drone.video_min_send_interval_ms`
- video source path in `/etc/hesia/hesia.env`
- server disk and log directory availability

### 8.4 Validator warns about boot measure, asset manifest, A/B, or RPMB

On the current Jetson SD profile, these warnings are expected.

They are profile warnings, not runtime failures.

### 8.5 Service files cannot be replaced

Cause:

- immutable attribute on the unit file

Fix:

```bash
chattr -i /etc/systemd/system/hesia-drone.service 2>/dev/null || true
systemctl daemon-reload
```

### 8.6 M2B Jetson benchmark fails

Check:

- the ONNX artifact is monolithic and not missing a hidden `.data` sidecar
- `python3 -c "import tensorrt, numpy"` works on Jetson
- `/home/ajax/.cache/.hesia/ml/hesia_m2b_jetson/` is writable

Expected current target profile:

- `tensorrt` available
- `numpy` available
- `torch` absent
- `onnxruntime` absent
- `trtexec` absent

Primary action:

- re-export the ONNX artifact with `ml/hesia_m2b/export_onnx.py`
- rerun `tools/jetson_benchmark_m2b.sh`
- inspect `artifacts/jetson_ml/hesia_m2b_tensorrt_fp16.json`

## 9. Incident handling

If the system behaves unexpectedly:

1. do not rebuild first
2. preserve `/var/log/hesia`
3. capture:
   - latest session log
   - `journalctl -u hesia-server.service`
   - `journalctl -u hesia-drone.service`
   - current `/etc/hesia/policy/policy.conf`
- current `/etc/hesia/secure/firmware_allowlist.txt`
- current `/etc/hesia/secure/firmware_allowlist.txt.sig`
4. verify whether the failure is:
   - transport
   - policy
   - allowlist
   - OP-TEE
   - video source
   - service hardening

## 10. Release hygiene

- never run the production services from a mutable build directory
- deploy runtime binaries to `/opt/hesia/bin`
- keep debug sidecars outside runtime execution paths
- keep release binaries immutable whenever maintenance is not actively underway
- clear the immutable bit only for the release maintenance window, then restore it after deployment
- refresh `/etc/hesia/secure/firmware_allowlist.txt` with the exact deployed drone hash after every drone binary replacement
- refresh `/etc/hesia/secure/firmware_allowlist.txt.sig` together with the allowlist text after every drone binary replacement
- re-run validation after every release install

## 11. M2B operations

The current embedded multimodal candidate can be revalidated on Jetson with:

```bash
./tools/jetson_benchmark_m2b.sh
```

Operational outputs:

- local benchmark JSON:
  - `artifacts/jetson_ml/hesia_m2b_tensorrt_fp16.json`
- remote workspace:
  - `/home/ajax/.cache/.hesia/ml/hesia_m2b_jetson`

Use this check after:

- changing the exported checkpoint
- changing ONNX export settings
- changing the Jetson image TensorRT stack
