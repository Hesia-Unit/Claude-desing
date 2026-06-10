# HESIA Jetson Transport Soak Report 2026-04-23

## Scope

This report closes the Jetson transport validation work for the current HESIA
release profile on `ajax-desktop` (`100.101.152.53`).

The objective was to prove, with artifacts and timestamps, that:

- the drone and server remain connected for sustained periods
- secure telemetry remains stable over minutes to hours
- `VIDEO_DATA` remains stable after the file-replay fix
- the final deployed drone binary no longer emits repeated OP-TEE
  `recovery_challenge` errors during the observed soak window

This report is intentionally evidence-driven and only reflects behavior
observed on the target.

---

## Target And Runtime Context

- Target: Jetson Orin Nano Super, hostname `ajax-desktop`
- Access path: Tailscale + SSH
- Active services:
  - `hesia-drone.service`
  - `hesia-server.service`
- Deployed binaries:
  - `/opt/hesia/bin/hesia_drone`
  - `/opt/hesia/bin/hesia_server_cpp`
- Runtime environment file:
  - `/etc/hesia/hesia.env`
- Replay source used during validation:
  - `/home/ajax/.cache/.hesia/src/videos/DRONE2.mp4`
- Live rebuild worktrees used for the validated binaries:
  - `/home/ajax/.cache/.hesia/work/runtime_20260420/drone_source`
  - `/home/ajax/.cache/.hesia/work/runtime_20260420/server_source`

---

## Test Method

The validation was performed in three successive phases.

Each phase relies on:

- `journalctl -u hesia-drone.service`
- `journalctl -u hesia-server.service`
- per-session server logs under `/var/log/hesia/drone/`
- generated transport artifacts under `artifacts/jetson_transport_soak/`
- post-analysis using `tools/analyze_jetson_transport_artifact.py`

The same methodology was kept across phases so that the observed differences
remain attributable to the deployed fixes.

---

## Phase 1: Long Transport Stability Before Replay Fix

### Artifact

- `artifacts/jetson_transport_soak/2026-04-23_20-23-52_final/summary.json`

### Primary Session

- Session: `100.101.152.53:47632`
- First log: `2026-04-23T19:17:33+0200`
- Last log: `2026-04-23T20:23:35+0200`
- Covered duration: `3962` seconds, about `66.0` minutes

### Observed Counts

- `SECURE_SESSION` markers: `1`
- Telemetry success lines: `3933`
- Video success lines: `256`
- Maximum `VIDEO_DATA` total observed: `2705`
- Session errors: `0`
- Decrypt failures: `0`
- TLS failures: `0`
- Transport write failures: `0`

### Correlated Behavior

This phase proved that the secure session and telemetry transport were already
stable for more than one hour.

The important limit exposed by this phase was not the secure transport itself,
but the file-replay behavior:

- replay-loop iterations observed: `0`
- EOF events started at `2026-04-23T19:26:59+0200`
- EOF events then repeated continuously for the rest of the window

Interpretation:

- secure transport stayed alive
- telemetry kept flowing
- the video source exhausted because the deployed binary did not yet include the
  correct replay-loop path

This phase therefore isolated the issue to the video-source runtime path rather
than to TLS, HESIA, or the secure channel.

---

## Phase 2: Replay-Loop Validation After Video Fix

### Artifact

- `artifacts/jetson_transport_soak/2026-04-23_20-37-38_manual/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_20-37-38_manual/report.md`

### Primary Session

- Session: `100.101.152.53:53474`
- First log: `2026-04-23T20:37:50+0200`
- Last log: `2026-04-23T20:54:38+0200`
- Covered duration: `1008` seconds, about `16.8` minutes

### Observed Counts

- Samples collected: `16`
- Drone active on all samples: `true`
- Server active on all samples: `true`
- Telemetry success lines: `1015`
- Video success lines: `423`
- Maximum `VIDEO_DATA` total observed: `5820`
- EOF events: `0`
- Transport write failures: `0`
- Session errors: `0`
- Decrypt failures: `0`
- TLS failures: `0`

### Correlated Behavior

This phase validated the replay-loop fix on the Jetson target.

Observed replay-loop timestamps:

- `2026-04-23T20:43:43+0200`
- `2026-04-23T20:53:10+0200`

Interpretation:

- the file replay now loops correctly
- video transport continues across loop boundaries
- there is no return of the prior EOF storm

Residual issue still visible during this phase:

- repeated OP-TEE `recovery_challenge` errors were still present in logs
- count observed by analysis: `103`

This error path did not break transport, but it remained noisy and had to be
fixed before calling the Jetson runtime cleanly finalized.

---

## Phase 3: Final Deployed-Binary Validation After Attestation Noise Fix

### Artifact

- `artifacts/jetson_transport_soak/2026-04-23_21-04-16_manual/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_21-04-16_manual/report.md`

### Primary Session

- Session: `100.101.152.53:42824`
- First log: `2026-04-23T21:04:28+0200`
- Last log: `2026-04-23T21:16:09+0200`
- Covered duration: `701` seconds, about `11.7` minutes

### Observed Counts

- Samples collected: `12`
- Drone active on all samples: `true`
- Server active on all samples: `true`
- Telemetry success lines: `716`
- Video success lines: `335`
- Maximum `VIDEO_DATA` total observed: `3889`
- EOF events: `0`
- Transport write failures: `0`
- Session errors: `0`
- Decrypt failures: `0`
- TLS failures: `0`
- `recovery_challenge` errors during the soak window: `0`
- pinned attestation-pubkey fallback warnings during the soak window: `0`

### Correlated Behavior

Observed replay-loop timestamp:

- `2026-04-23T21:12:00+0200`

Interpretation:

- the final deployed drone binary preserves the replay-loop fix
- secure transport remains stable
- telemetry and video continue together
- the repeated OP-TEE attestation noise is gone from the soak window

One important nuance remains:

- after the true process restart, a single startup warning was observed once for
  process `30320`
- that warning indicates fallback to the pinned P-256 attestation public key
  because the `recovery_challenge` export path is unavailable on this target

That behavior is now bounded and non-repeating inside the final validation
window.

---

## Phase 4: Fresh One-Hour Final Soak On The Restarted Final Build

### Artifact

- `artifacts/jetson_transport_soak/2026-04-23_21-55-59/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_21-55-59/report.md`

### Context

This phase starts from the final deployed binaries after an explicit service
restart and was collected with:

- `60` sampled checkpoints
- a fresh server session `100.101.152.53:46282`
- a post-capture artifact rehydrated with correctly encoded `journalctl` logs

### Primary Session

- Session: `100.101.152.53:46282`
- First log: `2026-04-23T21:56:15+0200`
- Last log: `2026-04-23T23:03:39+0200`
- Covered duration: `4044` seconds, about `67.4` minutes

### Observed Counts

- Samples collected: `60`
- Drone active on all samples: `true`
- Server active on all samples: `true`
- Telemetry success lines: `4006`
- Video success lines: `1724`
- Maximum `VIDEO_DATA` total observed: `21025`
- Session errors: `0`
- Decrypt failures: `0`
- TLS failures: `0`
- Transport write failures: `0`
- EOF events: `0`
- `recovery_challenge` errors observed: `0`
- pinned attestation-pubkey fallback warnings observed inside the soak window: `0`

### Correlated Behavior

Observed replay-loop timestamps:

- `2026-04-23T21:59:28+0200`
- `2026-04-23T22:08:57+0200`
- `2026-04-23T22:18:24+0200`
- `2026-04-23T22:27:54+0200`
- `2026-04-23T22:37:21+0200`
- `2026-04-23T22:46:48+0200`
- `2026-04-23T22:56:17+0200`

Interpretation:

- the restarted final build stays healthy for more than one hour
- replay looping remains stable over seven observed loop iterations
- telemetry and video both continue throughout the soak window
- no transport break, no decrypt failure, and no repeated OP-TEE recovery noise
  were observed

This phase is the strongest final proof collected on the deployed Jetson
release.

---

## Cross-Phase Correlation

The three phases together establish the following progression:

1. Phase 1 proved long secure-session and telemetry stability for about
   `66 minutes`, while isolating the remaining fault to file replay.
2. Phase 2 proved that the replay-loop fix removed EOF churn and sustained video
   across at least two replay iterations.
3. Phase 3 proved that the final deployed binary keeps the replay-loop fix and
   eliminates repeated OP-TEE attestation noise from the observed soak window.
4. Phase 4 proved the same final build on a fresh restarted session for about
   `67.4 minutes`, with `60` samples, `7` replay loops, and no transport or
   recovery-path regressions.

This is the required fine-grained transport correlation:

- transport itself was not the root cause of the earlier video issue
- the earlier issue was source-replay behavior
- once replay was fixed, transport remained stable through multiple loop
  boundaries
- once the attestation fallback path was bounded to a single startup warning,
  the runtime became operationally clean in the observed final window

---

## Final Result

For the current Jetson release profile, the transport path is considered
validated with evidence.

Specifically:

- drone to server secure session: validated
- sustained telemetry: validated
- sustained `VIDEO_DATA` after replay fix: validated
- replay-loop continuity over multiple iterations: validated
- no repeated `recovery_challenge` errors in the final soak window: validated
- fresh restarted final build stable for about `67.4 minutes`: validated

The Jetson target is therefore operationally closed for this release profile,
with one honest residual note:

- there is no RPMB on this SD-based Jetson profile
- the current OP-TEE target still requires a single startup fallback to the
  pinned P-256 attestation public key when the export command is unavailable

Neither point invalidates the measured transport result; both must remain
documented as part of the target truth.

---

## Artifact Index

- `artifacts/jetson_transport_soak/2026-04-23_20-23-52_final/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_20-37-38_manual/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_20-37-38_manual/report.md`
- `artifacts/jetson_transport_soak/2026-04-23_21-04-16_manual/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_21-04-16_manual/report.md`
- `artifacts/jetson_transport_soak/2026-04-23_21-55-59/summary.json`
- `artifacts/jetson_transport_soak/2026-04-23_21-55-59/report.md`
- `tools/jetson_transport_soak.py`
- `tools/analyze_jetson_transport_artifact.py`
