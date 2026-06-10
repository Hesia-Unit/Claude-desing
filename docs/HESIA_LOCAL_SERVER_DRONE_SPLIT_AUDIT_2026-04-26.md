# HESIA Local-Server / Remote-Drone Audit

Date: 2026-04-26

## Scope

This note captures the final operating posture where:

- the HESIA server runs on the local workstation inside WSL
- the HESIA operator UI runs locally on loopback only
- the drone runtime runs on `ajax-desktop`
- the drone connects to the workstation through Tailscale on TCP `9000`

## Verified runtime state

- Local server process listens on `0.0.0.0:9000` inside WSL and is exposed to the tailnet through `tailscale serve`
- Local UI listens on `127.0.0.1:8080` only
- The drone service on `ajax-desktop` is active and reaches the local server
- Server logs confirm:
  - `HELLO/ACK/KEY_INIT ok`
  - `KEY_EXCHANGE ok`
  - `DRONE_AUTH ok, SERVER_AUTH sent`
  - `CONFIRM received`
  - `SECURE_SESSION established`
  - recurring `CONST telemetry update ok`
  - recurring `VIDEO_DATA ok`

## UI remediation

The operator UI was reworked to reduce false negative video alarms and present a more professional console.

Implemented changes:

- muted visual palette and reduced cyberpunk styling
- clearer session / live-feed / freshness indicators
- stable telemetry cards with `N/A` handling for unavailable values
- filesystem refresh no longer resets the current navigation path
- video retrieval now follows `frame_meta.json` updates instead of hammering `/api/frame`
- live image polling is throttled to a low operator-console cadence to stay under API rate limiting
- static asset cache busting added through versioned query strings

Root cause of the previous `NO FEED` symptom:

- the browser was requesting the JPEG endpoint too aggressively
- the UI could enter a false degraded state through overlapping image loads and API rate limiting

## Security review of the split topology

No new critical network-facing weakness was introduced beyond the minimum required for this operating mode.

Positive controls:

- UI remains loopback-only
- remote Jetson server service is stopped and removed
- the drone keeps only the CA, client cert/key, and pinned server public key required for client mode
- the server private ML-DSA key is no longer present on `ajax-desktop`
- the server TLS private key is no longer present on `ajax-desktop`
- the allowlist signing private key is no longer present on `ajax-desktop`

Residuals that remain honest and intentional:

- the local workstation server still runs with software ML-DSA signing, because this host has no OP-TEE-backed server TEE path
- the WSL server process binds `0.0.0.0` inside the Linux guest; the externally reachable path is still constrained by `tailscale serve`
- the drone must keep the pinned `server_public.bin` so it can authenticate the local server

## Server files removed from ajax-desktop

Removed:

- `/opt/hesia/bin/hesia_server_cpp`
- `/opt/hesia/bin/hesia_ui_server.py`
- `/etc/systemd/system/hesia-server.service`
- `/etc/hesia/certs/server.crt`
- `/etc/hesia/certs/server.key`
- `/etc/hesia/secure/server_secret.bin`
- `/etc/hesia/secure/allowlist_signing.key`
- server build trees and server sources under `/home/ajax/.cache/.hesia/...`
- stale server public key backups under `/etc/hesia/secure`

Retained on `ajax-desktop` because the drone still needs them:

- `/etc/hesia/certs/ca.crt`
- `/etc/hesia/certs/drone.crt`
- `/etc/hesia/certs/drone.key`
- `/etc/hesia/secure/server_public.bin`
- drone identity / TEE / sealed files

## Conclusion

The system is now operating in the intended split mode:

- server and UI on the workstation
- drone only on `ajax-desktop`
- no unnecessary server runtime or server private material left on the drone target

This is the correct posture for the current deployment model.
