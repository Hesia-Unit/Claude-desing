# HESIA TA / OP-TEE Reference

## Scope

This document is the command-level reference for the trusted application, the host tool, and the OP-TEE client surface used by the drone and server runtimes.

Primary code references:

- [ta_hesia.h](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/optee_ta_skeleton/ta/include/ta_hesia.h)
- [ta_hesia.c](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/optee_ta_skeleton/ta/ta_hesia.c)
- [main.c](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/optee_ta_skeleton/host/main.c)
- [optee_client.hpp](/C:/Users/matis/Documents/Hesia-Firmware/drone_source/optee_client.hpp)
- [optee_client.cpp](/C:/Users/matis/Documents/Hesia-Firmware/drone_source/optee_client.cpp)

## 1. TA identity

TA UUID:

- `a17de805-9dc1-43ef-932b-91f107cad57b`

## 2. Command map

Defined command ids:

- `0x0001` `TA_HESIA_CMD_SEAL`
- `0x0002` `TA_HESIA_CMD_UNSEAL`
- `0x0003` `TA_HESIA_CMD_ROTATE_KEY`
- `0x0004` `TA_HESIA_CMD_WIPE_KEY`
- `0x0005` `TA_HESIA_CMD_HKDF`
- `0x0006` `TA_HESIA_CMD_CHECK_VERSION`
- `0x0007` `TA_HESIA_CMD_RESET_VERSION`
- `0x0008` `TA_HESIA_CMD_READ_VERSION`
- `0x0009` `TA_HESIA_CMD_EXPORT_ATTEST_PUBKEY`
- `0x000A` `TA_HESIA_CMD_SIGN_ATTEST_DIGEST`
- `0x000B` `TA_HESIA_CMD_SET_SESSION_AUTH_SECRET`
- `0x000C` `TA_HESIA_CMD_STAGE_SLOT_UPDATE`
- `0x000D` `TA_HESIA_CMD_COMMIT_SLOT_BOOT`
- `0x000E` `TA_HESIA_CMD_READ_SLOT_META`
- `0x000F` `TA_HESIA_CMD_GET_RECOVERY_CHALLENGE`
- `0x0010` `TA_HESIA_CMD_RECOVER_SESSION_AUTH`
- `0x0011` `TA_HESIA_CMD_IMPORT_MLDSA_KEY_BLOB`
- `0x0012` `TA_HESIA_CMD_EXPORT_MLDSA_PUBKEY`
- `0x0013` `TA_HESIA_CMD_SIGN_MLDSA_PAYLOAD`
- `0x0014` `TA_HESIA_CMD_GET_MLDSA_STATUS`
- `0x0015` `TA_HESIA_CMD_GENERATE_MLDSA_KEYPAIR`

## 3. Security-critical roles

### 3.1 Sealing

The TA owns sealing and unsealing of runtime blobs. These paths are used for:

- session-auth secret material
- drone ML-DSA private identity
- other sealed runtime assets that must not live as plain files

### 3.2 Attestation

The TA exports attestation public material and signs attestation digests. The server binds this evidence to the drone identity during authentication.

### 3.3 ML-DSA

The TA is the production signing endpoint for ML-DSA.

Important slots:

- `HESIA_MLDSA_SLOT_DRONE`
- `HESIA_MLDSA_SLOT_SERVER`

Production meaning:

- drone identity signatures must come from the drone slot
- server `SERVER_AUTH` signatures must come from the server slot

### 3.4 Rollback metadata

The TA stores and exposes slot metadata used for staged update bookkeeping. On richer hardware profiles this is part of the rollback story. On the current Jetson SD profile, strict RPMB-backed enforcement is intentionally disabled by policy.

## 4. ML-DSA lifecycle

### 4.1 Import path

Import stages encoded in the TA API:

- `INIT`
- `ALLOC`
- `UNSEAL`
- `PARSE`
- `BACKEND_READY`
- `SELFTEST_SIGN`
- `SELFTEST_VERIFY`
- `PERSIST`
- `DONE`

Operational meaning:

- the TA can expose where an import failed
- operators can distinguish backend readiness from persistence failure

### 4.2 Key generation path

Keygen stages encoded in the TA API:

- `INIT`
- `ALLOC`
- `GENERATE`
- `SERIALIZE`
- `PERSIST`
- `DONE`

### 4.3 Current production posture

The active hardened posture is:

- ML-DSA signing performed in the TA
- soft-sign fallback disabled in production policy
- session authentication required before protected TA commands are accepted

## 5. Recovery flow

Recovery objects defined in the header:

- `hesia_recovery_challenge_t`
- `hesia_recovery_token_t`

Critical fields:

- challenge nonce
- attestation public key
- new secret hash
- current attestation public key hash
- recovery signature

Operational meaning:

- recovery is bound both to a fresh challenge and to the currently expected attestation anchor
- stale or mismatched recovery material must fail

## 6. Host-tool responsibilities

The OP-TEE host utility is the bridge used by scripts and operators for:

- provisioning session authentication
- exporting TA public anchors
- rotating TEE state
- importing ML-DSA material
- generating ML-DSA material
- reading status

Do not treat the host tool as a source of trust by itself.

The trust anchor remains the TA plus the secured deployment path around it.

## 7. Client-side runtime contract

The drone and server both rely on the OP-TEE client wrappers for:

- checking that session-auth is provisioned and readable
- importing or reading ML-DSA public keys
- signing payloads inside the TA
- reading slot metadata and update state

Production expectations:

- if the policy requires TEE signing and the TA is not ready, startup must fail
- if the session-auth blob is missing or unusable in production, startup must fail

## 8. Files and anchors tied to TA operations

Main runtime files:

- `/etc/hesia/secure/optee_session_auth.sealed`
- `/etc/hesia/secure/dilithium5_sk.sealed`
- `/etc/hesia/secure/dilithium5_pk.bin`
- `/etc/hesia/secure/drone_tee_attest_pub.bin`
- `/etc/hesia/secure/tee_attest_mldsa_pub.bin`

Operational rule:

- sealed private material stays under `/etc/hesia/secure`
- exported public anchors may also be mirrored into `/etc/hesia/keys`

## 9. Maintenance commands

Primary scripts:

- [export_ta_public_anchors.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/export_ta_public_anchors.sh)
- [export_server_slot_public_anchor.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/export_server_slot_public_anchor.sh)
- [provision_optee_session_auth.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/provision_optee_session_auth.sh)
- [rotate_drone_identity.sh](/C:/Users/matis/Documents/Hesia-Firmware/drone_transition_source/scripts/rotate_drone_identity.sh)

Expected sequence for a controlled identity rotation:

1. stop the drone service
2. back up current public and sealed material
3. rotate or provision session authentication
4. rotate TEE state
5. export fresh public anchors
6. export the server slot public anchor if the server signs from the OP-TEE `server` slot
7. copy fresh public anchors to the server secure directory
8. restart the drone service
9. verify a new secure session establishes

## 10. Failure modes to recognize quickly

- missing session-auth blob:
  - startup refusal before protected TA operations
- ML-DSA slot unavailable:
  - production startup refusal if TEE signing is required
- exported anchor mismatch:
  - handshake failure at drone authentication or server verification time
- stale `server_public.bin` while the server signs from the OP-TEE `server` slot:
  - handshake failure at `KEY_CONFIRM` before `DRONE_AUTH`
- stale allowlist / stale policy:
  - runtime starts but session fails at firmware or identity verification
