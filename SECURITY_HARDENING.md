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
4. Provision a TLS pin on the drone if `tls_pin=1`:
   - Supported inputs: a PEM/DER certificate, a PEM/DER public key, or a raw 32-byte SPKI SHA-256 digest.
   - Default lookup order: `secure_dir/server_tls_spki.pem`, `secure_dir/server_tls_spki.der`, `cert_dir/server.crt`.
5. Keep the UI local-only unless you explicitly set `HESIA_UI_ALLOW_REMOTE=1`.
6. Leave forensic capture disabled unless you are actively investigating an incident:
   - `HESIA_FORENSIC_MESSAGE_CAPTURE=1`
   - `HESIA_FORENSIC_VIDEO_CAPTURE=1`

## Hardened code paths

- Server policy verification now uses an embedded Ed25519 root key instead of trusting a replaceable filesystem public key.
- Server production mode now refuses demo ML-DSA keys.
- Server production mode now requires a pinned drone ML-DSA public key.
- Drone production mode now refuses to rely on the embedded demo server ML-DSA public key.
- TLS SPKI pinning now accepts real certificate/public-key material instead of hashing unrelated bytes.
- Invalid bind addresses no longer silently downgrade to `0.0.0.0`.
- Decrypted message/frame capture is opt-in.
- The UI now binds to `127.0.0.1` by default, clamps log requests, prevents path-prefix traversal mistakes, and no longer injects untrusted log/file names as HTML.
