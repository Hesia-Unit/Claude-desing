# HESIA Security Hardening Notes

This repository contained live sensitive material and several trust-chain weaknesses. The codebase has been hardened, but the deployment still requires immediate operational action.

## Immediate actions

1. Rotate every private key that was present in the repository:
   - `server_source/keys/ca.key`
   - `server_source/keys/server.key`
   - `server_source/keys/drone.key`
   - `server_source/keys/tls_ca.key`
   - `server_source/keys/tls_server.key`
   - `server_source/keys/demo_secret.bin`
   - `server_source/keys/demo_secret.pem`
   - `drone_transition_source/allowlist_priv.pem`
2. Provision unique ML-DSA server signing keys in production:
   - Server expects `server_secret.bin` and `server_public.bin` in the deployed secure keys directory.
3. Provision a pinned ML-DSA server public key on the drone:
   - Drone expects `server_public.bin` or `server_mldsa87_public.bin` in `secure_dir`.
4. Provision a pinned drone TEE attestation public key on the server:
   - Server expects `tee_attest_p256_pub.bin` (or the path configured by `drone_tee_pubkey_file`) in `secure_dir`.
5. Provision a measured-boot allowlist on the server:
   - Server expects `boot_measure_allowlist.txt` in `secure_dir`.
6. Configure a signed measured-boot manifest on the drone:
   - Required policy keys: `require_boot_measure=1`, `boot_measure_path`, `boot_measure_sig_path`, `boot_measure_pubkey_path`.
   - Recommended manifest fields: `format`, `source`, `secure_boot`, `firmware_version`, `binary_sha3_512`, `timestamp_unix`.
7. Provision OP-TEE session authentication on the drone:
   - Drone expects `optee_session_auth.sealed` in `secure_dir`.
   - Policy should keep `require_optee_session_auth=1`.
   - Bootstrap helper: `drone_transition_source/scripts/provision_optee_session_auth.sh`
   - Rotation helper: `drone_transition_source/optee_ta_skeleton/host/main.c` with `rotate_session_auth`, or `drone_transition_source/scripts/rotate_drone_identity.sh`.
8. Provision A/B slot metadata and keep rollback state on RPMB-backed OP-TEE storage:
   - Policy should keep `require_ab_slots=1` and `require_rpmb_rollback_storage=1`.
   - Stage helper: `drone_transition_source/scripts/stage_ab_update.sh`
   - Host tool commands: `stage_slot_update`, `commit_slot_boot`, `read_slot_meta`
   - Asset manifests must include a `slot=` field and a monotonic `version=`.
9. Keep production ML-DSA signing fail-closed until the TA/HSM signer is present:
   - Policy should keep `require_mldsa_sign_in_tee=1`.
   - Production boot now aborts if the build cannot prove ML-DSA signing is inside OP-TEE.
10. Run the rotation tooling before any production deployment:
   - Server-side staging: `server_source/tools/rotate_all_keys.sh`
   - Drone-side maintenance: `drone_transition_source/scripts/rotate_drone_identity.sh`
11. Provision a TLS pin on the drone if `tls_pin=1`:
   - Supported inputs: a PEM/DER certificate, a PEM/DER public key, or a raw 32-byte SPKI SHA-256 digest.
   - Default lookup order: `secure_dir/server_tls_spki.pem`, `secure_dir/server_tls_spki.der`, `cert_dir/server.crt`.
12. Keep the UI local-only unless you explicitly set `HESIA_UI_ALLOW_REMOTE=1`.
13. Leave forensic capture disabled unless you are actively investigating an incident:
   - `HESIA_FORENSIC_MESSAGE_CAPTURE=1`
   - `HESIA_FORENSIC_VIDEO_CAPTURE=1`

## Hardened code paths

- Server policy verification now uses an embedded Ed25519 root key instead of trusting a replaceable filesystem public key.
- Server production mode now refuses demo ML-DSA keys.
- Server production mode now requires a pinned drone ML-DSA public key.
- Server production mode now loads ML-DSA signing keys from `secure_dir` and only falls back to `keys_dir` outside production.
- Drone `DRONE_AUTH` signatures are now bound to the drone ML-DSA public key, the measured-boot digest, and the exported TEE attestation public key.
- Drone measured boot is now verified as a signed manifest and checked against the running binary hash, secure-boot state, firmware version, and optional freshness policy before it contributes to attestation.
- Drone PUF response is now challenge-bound to the session and transcript instead of being a static hash.
- Drone now exports a TEE-resident P-256 attestation public key and signs the `DRONE_AUTH` payload through OP-TEE.
- OP-TEE sessions now require a provisioned session-auth secret in production, the TA refuses normal sessions until that secret has been provisioned, and the drone now fails fast at startup if authenticated OP-TEE sessions are not ready.
- Firmware boot now commits the current A/B slot through OP-TEE slot metadata, and staged updates must be explicitly recorded before a new slot can become active.
- Production mode now refuses rollback protection if no RPMB device is visible from Linux.
- The repo now carries a reproducible-build verifier and CI job that rebuild selected firmware targets twice in clean trees and compares bit-for-bit artifacts.
- Drone ML-DSA signing no longer keeps the long-term private key resident in REE memory when OP-TEE anchoring is available; the key is unsealed only for the short signing window, page-locked during the actual signing call, and wiped immediately after use.
- Production policy now fails closed if ML-DSA signing is not fully delegated to OP-TEE.
- Server and drone now sign/verify the full `SERVER_AUTH` content, including pinned server key material and the signed policy hash.
- Drone production mode now refuses to rely on the embedded demo server ML-DSA public key.
- Firmware allowlists and drone revocation material now resolve from `secure_dir` first and only fall back outside production.
- Server now verifies the TEE attestation signature attached to `DRONE_AUTH`, and can pin the drone TEE key independently from the ML-DSA identity.
- Server now supports a dedicated measured-boot allowlist (`boot_measure_allowlist.txt`) in addition to the firmware hash allowlist.
- TLS SPKI pinning now accepts real certificate/public-key material instead of hashing unrelated bytes.
- Invalid bind addresses no longer silently downgrade to `0.0.0.0`.
- Decrypted message/frame capture is opt-in.
- The UI now binds to `127.0.0.1` by default, clamps log requests, prevents path-prefix traversal mistakes, and no longer injects untrusted log/file names as HTML.
- Secure-session decryption failures now tear down the session instead of letting a tampered peer continue.
- Jetson sync now excludes server-side secrets and private allowlist material by default.
- OP-TEE maintenance commands (`rotate`, `wipe`, `reset_version`) are now disabled by default in the TA skeleton, and maintenance rotation now renews both the sealing key and the TEE attestation key when enabled.
