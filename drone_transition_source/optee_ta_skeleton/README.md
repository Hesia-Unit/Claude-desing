# OP-TEE TA Skeleton

This folder provides a complete TA + a simple user-space client:
- `ta/` : Trusted Application (AES-256-GCM sealing, persistent internal key)
- `host/` : Client (TEEC) with `seal` / `unseal` commands

## Build on Jetson
1) Export TA dev kit:
   `export TA_DEV_KIT_DIR=/path/to/export-ta_arm64`
2) Build TA:
   `make -C ta`
3) Build client:
   `make -C host`

## Notes
- No raw key leaves the TA.
- Sealing uses AES-GCM (random nonce + tag).
