## Key Material Policy

No private keys or long-term symmetric secrets should be stored in this repository.

Provision these artifacts out-of-band on deployment targets:

- server ML-DSA signing keys in `secure_dir`
- pinned drone TEE attestation public keys in `secure_dir`
- drone OP-TEE session-auth secrets in `secure_dir`
- audit symmetric/signing keys in `secure_dir`
- TLS private keys in `cert_dir`
- CA private material in an offline PKI workflow
- signed firmware and measured-boot allowlists in `secure_dir`
- demo key material outside the repository, only for explicit non-production labs

If any of the deleted files were ever used outside a throwaway lab, treat them as compromised and rotate them immediately.
