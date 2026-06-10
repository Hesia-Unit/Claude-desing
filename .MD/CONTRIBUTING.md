# Contributing

## Security-first rules

- Never commit private keys, sealed blobs, operator secrets, or recovery tokens.
- Never weaken `prod_fuse` behavior to make a deployment "temporarily work".
- Keep all new crypto, TEE, policy, and transport changes covered by tests or a written validation step.
- Treat Jetson SD-boot limitations honestly in docs and policy defaults.

## Change process

1. Make the smallest safe change that closes the issue.
2. Validate locally with the relevant lint/build/test tools.
3. If the change affects Jetson runtime, validate on the target before calling it complete.
4. Update docs or the operations runbook when operator behavior changes.

## Required checks

- `python3 tools/check_opaque_binaries.py --repo .`
- `python3 tools/check_declared_dependencies.py --repo .`
- `python3 tools/check_versioned_private_keys.py --repo .`
- `python3 tools/generate_firmware_sbom.py --repo . --output artifacts/firmware-sbom.json`
- `python3 tools/check_doc_links.py --root .`
- `shellcheck` on modified shell scripts

## Reviews

- Security-sensitive paths require review from both firmware and security ownership:
  - `drone_source/`
  - `server_source/src/`
  - `drone_transition_source/optee_ta_skeleton/`
  - `security/`
  - `tools/`
