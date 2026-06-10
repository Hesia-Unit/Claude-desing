# HESIA Security Remediation Status

Date: `2026-04-25`  
Scope: remediation pass for the findings listed in `/TODO`

## Final validation snapshot

- Local script validation:
  - `python -m py_compile tools/check_doc_links.py tools/check_versioned_private_keys.py tools/generate_firmware_sbom.py tools/jetson_transport_soak.py tools/render_markdown_docx.py`
  - `bash -n tools/refresh_firmware_allowlist.sh tools/build_hesia_cloaked_release.sh tools/harden_release_binary.sh tools/deploy_hesia_release.sh drone_transition_source/AIR/IMPLEMENTATION/Jetson/GPT/drone/tools/hesia-validate.sh`
- Jetson validation:
  - `hesia-validate.sh`: `Pass: 28  Warn: 5  Fail: 0`
  - Secure session restored and stable after the final fixes.
  - Loader proof: `ldd /opt/hesia/bin/hesia_drone` resolves `libhesia_sentinel.so` from `/opt/hesia/lib`.
  - Deployed binary proof: `readelf -d /opt/hesia/bin/hesia_drone` shows `NEEDED=libhesia_sentinel.so` and `RUNPATH=$ORIGIN/../lib:/usr/local/cuda/lib64`.
  - Fresh transport soak artifact:
    - [report.md](/C:/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_transport_soak/2026-04-25_09-21-44/report.md)
    - [summary.json](/C:/Users/matis/Documents/Hesia-Firmware/artifacts/jetson_transport_soak/2026-04-25_09-21-44/summary.json)

## P0 and P1 findings

| Finding | Status | Result |
| --- | --- | --- |
| `Faille_01_RPMB_absent_rollback_TEE.md` | hardware-limited | Jetson Orin Nano Super SD boot exposes no RPMB. The firmware now fails honestly when RPMB is required, and the deployed Jetson policy explicitly runs without RPMB for this platform revision. |
| `Faille_02_OPTEE_bootstrap_TOFU.md` | fixed | OP-TEE session authentication now requires provisioned session-auth material; bootstrap and recovery were hardened and validated on target. |
| `Faille_03_TLS_fallback_clair.md` | fixed | Clear transport fallback removed; drone now fails closed when the secure transport path is unavailable. |
| `Faille_04_AES_GCM_nonce_reuse.md` | fixed | Directional keys, directional IV prefixes, authenticated epochs, replay window hardening, and no weak RNG fallback remain in place and are covered by unit tests. |
| `Faille_05_HKDF_HMAC_SHA3_maison.md` | fixed | Runtime KDF paths use OpenSSL EVP HKDF on the normal-world side; TA HKDF path was hardened and the policy handles the platform capability honestly. |
| `Faille_06_TA_sealing_AES_GCM_sans_AAD.md` | fixed | OP-TEE sealing now binds AAD and supports safe legacy migration only for old sealed blobs. |
| `Faille_07_pas_de_separation_sender_receiver.md` | fixed | `SecureChannel` now derives and enforces distinct tx/rx keys and IV material by role. |
| `Faille_08_anti_replay_apres_decryption.md` | fixed | Replay pre-filter now runs before decrypt and uses a sliding bitmap window. |
| `Faille_09_decrypt_resize_non_borne.md` | fixed | Ciphertext/plaintext bounds are enforced before allocation and decrypt. |
| `Faille_10_demo_keys_embarquees.md` | fixed | Demo/embedded production secrets were removed from active runtime paths; production now loads from provisioned secure material only. |
| `Faille_11_TA_wipe_key_internal_bug.md` | fixed | TA wipe/rotation paths were repaired and validated during the OP-TEE hardening pass. |
| `Faille_12_TA_attest_P256_non_HW_bound.md` | fixed | Production now requires the ML-DSA TEE attestation path; legacy P-256 attestation is rejected in production mode. |
| `Faille_13_TA_recovery_nonce_replay.md` | mitigated | Recovery tokens now bind nonce, current attestation key hash, and a TEE-issued expiry. Residual replay risk remains hardware-limited without monotonic trusted state. |
| `Faille_14_TA_maintenance_cmds_sans_signature.md` | fixed | Maintenance commands are no longer open by default and require hardened authenticated access paths. |
| `Faille_15_TA_sign_attest_digest_accepte_court.md` | fixed | Digest length validation is strict in TA and client. |
| `Faille_16_server_accept_loop_DoS.md` | fixed | Server-side transport/rate limiting and session failure handling were hardened. |
| `Faille_17_KDF_SP800_108_separateur_ambigu.md` | fixed | Label/context validation now rejects ambiguous separator cases. |
| `Faille_18_server_allowlist_revoke_sans_signature.md` | fixed | Control-list loading now requires detached Ed25519 signatures, including allowlist/revocation data. |
| `Faille_19_server_frames_world_readable.md` | fixed | Saved-frame output permissions were hardened server-side. |
| `Faille_20_HESIA_FORENSIC_env_bypass.md` | fixed | Production rejects forensic override env paths. |
| `Faille_21_server_rotate_all_keys_shell_injection.md` | fixed | Rotation scripts were hardened and shell injection surfaces removed. |
| `Faille_22_server_UI_non_authentifiee.md` | fixed | Local UI now has auth/header hardening, bind constraints, and TLS support with remote-start refusal unless cert/key are configured. |
| `Faille_23_video_source_path_traversal.md` | fixed | File replay paths are canonicalized and symlink/traversal constrained. |
| `Faille_24_secrets_non_zeroises_RAM.md` | mitigated | Sensitive buffers now get explicit zeroization plus broader `mlock`/`MADV_DONTDUMP` protection, but a universal secure allocator is still not deployed everywhere. |
| `Faille_25_cle_SSH_privee_sur_disque.md` | operationally mitigated | Not a firmware bug. Repo-side mitigation is in place (`.gitignore`, docs, operations guidance). Workstation ACL and key custody remain an operator responsibility. |

## P2 grouped findings

| Item | Status | Result |
| --- | --- | --- |
| `P2-01` verbose production logs | fixed | Transport prefix leakage is now debug-gated; sensitive channel prefixes no longer appear in deployed Jetson logs. |
| `P2-02` missing SRI on UI | not applicable | Current shipped UI is self-hosted and CSP-hardened; no third-party CDN script is referenced. |
| `P2-03` vendor deps without CVE tracking | fixed | CI now includes dependency automation and security scanning. |
| `P2-04` no SBOM | fixed | SBOM generation is in the build/release pipeline. |
| `P2-05` non-reproducible release flags | fixed | `SOURCE_DATE_EPOCH`, path remapping, and reproducibility tooling are present. |
| `P2-06` unversioned policy schema | fixed | Strict `schema_version=1`, unknown-key rejection, duplicate-key rejection. |
| `P2-07` test scripts without isolation | not applicable | The referenced `tools/tests` layout is not present anymore. |
| `P2-08` no SafeStack / ShadowCallStack | fixed | CMake now probes and supports both hardening modes when toolchain/runtime allow it. |
| `P2-09` no `FORTIFY_SOURCE=3` | fixed | Drone and server CMake now set `-D_FORTIFY_SOURCE=3`. |
| `P2-10` partial RELRO | fixed | Full RELRO / immediate binding is enabled in hardened builds. |
| `P2-11` writable exec path under service | fixed | Drone now loads `libhesia_sentinel.so` from `/opt/hesia/lib`, and the writable workspace is in `NoExecPaths` on Jetson. |
| `P2-12` seccomp profile too permissive | fixed | seccomp arg filtering and service sandboxing were tightened. |
| `P2-13` no AppArmor / SELinux profile | fixed | AppArmor profiles are now shipped in `security/apparmor/`. |
| `P2-14` crypto unit coverage partial | fixed | `tests/unit/hesia_secure_channel_tests` covers replay/rotation/directional material. |
| `P2-15` no continuous fuzzing | fixed | CI includes fuzz harnesses and a nightly fuzz job. |

## P3 grouped findings

| Item | Status | Result |
| --- | --- | --- |
| `P3-01` first-party TODO/FIXME debt | fixed | First-party critical TODO/FIXME debt was triaged; remaining hits are in vendored `liboqs` or non-runtime assets. |
| `P3-02` hardcoded internal identity | not applicable | The cited `valstrax` identity is no longer present in first-party runtime scripts. |
| `P3-03` incomplete `.gitignore` | fixed | IDE and local artifacts were added. |
| `P3-04` missing branch/review policy docs | fixed | `CONTRIBUTING.md` and `CODEOWNERS.sample` were added. |
| `P3-05` missing third-party license aggregation | fixed | `THIRD_PARTY_LICENSES.md` was added. |
| `P3-06` stale documentation links | fixed | Doc link checking was added and docs were refreshed. |
| `P3-07` endian portability spots | accepted | No confirmed exploitable first-party wire-format issue remained in the audited paths; keep under regression review. |
| `P3-08` `random_device` in tests | fixed | First-party runtime paths no longer rely on `std::random_device`; remaining usage is confined to fuzz/test code. |
| `P3-09` missing `.editorconfig` | fixed | `.editorconfig` added. |
| `P3-10` duplicated TA command strings | not applicable | The specific duplicated symbols cited by the audit were not present in the current first-party code state. |
| `P3-11` missing observability metrics | partially fixed | Soak/transport observability tooling exists, but this is still weaker than a full authenticated metrics endpoint. |
| `P3-12` no explicit project `LICENSE` | external | Legal/product decision, not safely auto-fixable without owner intent. |

## Additional hardening completed during this pass

- Added [tools/refresh_firmware_allowlist.sh](/C:/Users/matis/Documents/Hesia-Firmware/tools/refresh_firmware_allowlist.sh) so allowlist refreshes now write the correct detached signature path: `firmware_allowlist.txt.sig`.
- Fixed the Jetson release packaging so the deployed drone no longer depends on a writable workspace path for Sentinel:
  - deployed binary `RUNPATH`: `$ORIGIN/../lib:/usr/local/cuda/lib64`
  - deployed `NEEDED`: `libhesia_sentinel.so`
  - CMake now sets `IMPORTED_NO_SONAME` and `IMPORTED_SONAME=libhesia_sentinel.so` for the imported Sentinel target so new builds no longer regenerate an absolute `DT_NEEDED`
- Corrected recurring runtime noise by disabling repeated legacy runtime-attestation spam after a permanent TEE signing failure in the best-effort report path.
- Recovery hardening now persists a v2 recovery state with TEE-side expiry (`expires_at_sec`) and verifies that expiry on recovery token consumption.
- Secret-memory hygiene now uses `SecureMemory::protect()` for critical vectors/strings, with `mlock` and `MADV_DONTDUMP` on Linux where available.
- Jetson seccomp policy was updated so the operational drone profile admits the memory syscalls required by the real runtime:
  - `mlock`
  - `munlock`
  - `mlock2` when available
  - executable `mmap` / `mprotect` only in `DRONE_OPERATIONAL`, to keep TensorRT/CUDA functional while preserving stricter no-exec memory rules in non-operational profiles
- Added TLS support and stricter startup gating to [ui_server.py](/C:/Users/matis/Documents/Hesia-Firmware/server_source/tools/ui_server.py):
  - `HESIA_UI_TLS_CERT`
  - `HESIA_UI_TLS_KEY`
  - `HESIA_UI_REQUIRE_TLS`
  - remote or TLS-required starts now fail closed if certificates are missing.
- Created an offline Jetson audit image under [F:\Image-Jetson\2026-04-24_22-19-09\reports\README.md](F:\Image-Jetson\2026-04-24_22-19-09\reports\README.md) with root snapshots, workspace snapshot, manifests, hashes, packages, and service inventory.

## Residual accepted limits

- No RPMB on this Jetson SD-boot platform.
- Recovery anti-replay is hardened but not backed by hardware monotonic state.
- Some runtime warnings remain informational rather than exploit-bearing:
  - system ASLR warning versus kernel setting
  - inactive ASAN/TSAN/fuzz instrumentation in production builds
  - missing EM sensor on this platform
  - Jetson GPU stack still emits TensorRT engine portability warnings for the shipped plan files

## Operational conclusion

For the `/TODO` findings that were valid on the current system:

- firmware/code defects were corrected
- Jetson deployment was revalidated
- the drone/server secure session was restored after remediation
- security validation now ends with no hard failure on the target

The remaining items are either:

- platform-hardware limits on Jetson,
- intentionally documented production-policy tradeoffs,
- or external operational/legal items outside the firmware codebase itself.
