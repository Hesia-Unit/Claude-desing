`allowlist.sig` is stored as base64 text so the repository does not carry opaque raw signature blobs.

When a raw signature is required operationally, decode it during provisioning rather than versioning the binary blob directly.
