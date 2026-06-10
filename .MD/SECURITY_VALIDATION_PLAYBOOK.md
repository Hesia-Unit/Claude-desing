# HESIA Security Validation Playbook

This repository now contains the code hooks needed for measured boot verification, TEE-backed attestation, and staged key rotation. A real external audit and a serious pentest still require independent operators, deployed Jetson targets, and the final production image.

## What was completed in the codebase

- Measured boot is validated as a signed manifest on the drone and checked against the running binary hash.
- `DRONE_AUTH` is now bound to:
  - the drone ML-DSA public key
  - the firmware hash
  - the measured-boot digest
  - the session/transcript/TLS bindings
  - a TEE-resident P-256 attestation key
- The server can pin and verify the drone TEE public key separately from the ML-DSA identity.
- Rotation scripts were added for deployment staging and drone maintenance.

## What cannot be honestly claimed from this workstation alone

- An external audit is not “performed” until an independent team reviews:
  - the source
  - the build pipeline
  - the deployed Jetson image
  - fuse state / secure-boot state
  - OP-TEE packaging and provisioning
  - the operational runbooks
- A serious pentest is not “performed” until a red team attacks the real target environment, not just the repository.

## Mandatory external audit scope

1. Secure boot chain on Jetson Orin Nano Super
   - Fuse state
   - UEFI / bootloader chain
   - Kernel / initramfs trust
   - OP-TEE loading and persistence model
2. Cryptographic review
   - ML-DSA and ML-KEM integration
   - TEE attestation key lifecycle
   - Rotation procedures and revocation latency
3. Supply-chain and build integrity
   - Reproducible builds or equivalent provenance
   - Artifact signing
   - Offline key ceremony
4. Platform abuse resistance
   - Physical extraction attempts
   - Debug/UART/JTAG exposure
   - Log leakage and forensic storage hygiene

## Mandatory pentest scenarios

1. MITM against TLS + transcript binding + `DRONE_AUTH`
2. Replay of old `DRONE_AUTH` and measured-boot artifacts
3. Drone identity swap:
   - replace ML-DSA key only
   - replace TEE key only
   - replace both and test server pinning failures
4. Rollback and mixed-version boot attempts
5. Boot manifest forgery, stale manifest reuse, and fake secure-boot reporting
6. OP-TEE abuse:
   - session opening from untrusted REE code
   - maintenance command exposure
   - sealed-object tampering
7. Denial-of-service on key exchange, attestation, and video/control channels

## Acceptance gates before claiming high-assurance deployment

- Independent audit findings closed or formally risk-accepted
- Pentest rerun on the release candidate image
- Rotated keys provisioned outside the repository
- `firmware_allowlist.txt`, `boot_measure_allowlist.txt`, and `revoked_drones.txt` managed as signed operational artifacts
- Final deployment validated on-device with `hesia-validate.sh`
