# Third-Party Licenses

This file tracks the principal third-party components bundled or required by the HESIA firmware workspace.

## Bundled in the repository

### liboqs

- Location: `server_source/liboqs`
- Upstream license file: `server_source/liboqs/LICENSE.txt`
- Notes: liboqs also vendors multiple upstream PQC implementations with algorithm-specific `LICENSE` files under `server_source/liboqs/src/**`.

### ML-DSA reference backend for the TA

- Location: `drone_transition_source/optee_ta_skeleton/ta/third_party/mldsa87_ref`
- Notes: this workspace does not currently expose a top-level license file for that imported subtree. Treat license verification as mandatory whenever the backend is updated.

## System or operator-provided dependencies

These are not fully vendored here and must be tracked at release time in the SBOM:

- OpenSSL
- OP-TEE client/runtime
- libseccomp
- OpenCV
- TensorRT / CUDA

## Release rule

Every production release must ship:

- the generated SBOM (`artifacts/firmware-sbom.json`)
- this license index
- the upstream license texts required by any bundled component
