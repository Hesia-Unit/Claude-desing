# HESIA Documentation Set

This directory is the handover package for engineering, security, firmware, and operations teams.

Primary documents:

- `HESIA_CORELIB_FR.md`: French open source reference for HESIA CoreLib, the public C ABI library for security, post-quantum provider integration, watchdogs, inference guards, and language bindings.
- `HESIA_CORELIB_EN.md`: English open source reference for HESIA CoreLib, the public C ABI library for security, post-quantum provider integration, watchdogs, inference guards, and language bindings.
- `HESIA_CORELIB_FR.pdf`: printable French PDF export of the CoreLib reference.
- `HESIA_CORELIB_EN.pdf`: printable English PDF export of the CoreLib reference.
- `HESIA_COMPLETE_REFERENCE_FR.md`: comprehensive French handover reference covering architecture, security, deployment, operations, maintenance, and AI/R&D status.
- `HESIA_COMPLETE_REFERENCE_EN.md`: comprehensive English handover reference covering architecture, security, deployment, operations, maintenance, and AI/R&D status.
- `HESIA_COMPLETE_REFERENCE_FR.docx`: presentable Word export of the French complete reference for engineering handover and offline review.
- `HESIA_COMPLETE_REFERENCE_EN.docx`: presentable Word export of the English complete reference for engineering handover and offline review.
- `HESIA_ENGINEERING_MANUAL.md`: end-to-end architecture, build, provisioning, security model, OP-TEE/TA behavior, and release flow.
- `HESIA_INSTALLATION_GUIDE.md`: clean-machine installation, provisioning, release deployment, and first-start validation.
- `HESIA_OPERATIONS_RUNBOOK.md`: day-2 operations, maintenance, incident handling, rotation, and troubleshooting.
- `HESIA_TA_OPTEE_REFERENCE.md`: TA command map, slot model, recovery flow, and OP-TEE client contract.
- `HESIA_MAINTENANCE_MATRIX.md`: symptom-to-action operational matrix for on-call and platform responders.
- `HESIA_JETSON_BASELINE_2026-04-20.md`: validated Jetson Orin Nano Super baseline with exact paths, services, hardening state, and runtime proof collected on 2026-04-20.
- `HESIA_JETSON_TRANSPORT_SOAK_2026-04-23.md`: long-duration Jetson transport validation with cross-phase correlation, replay-loop proof, and final soak evidence.
- `HESIA_M2B_JETSON_BENCHMARK_2026-04-21.md`: Jetson-side TensorRT benchmark path for the compact multimodal candidate.
- `HESIA_M2B_VISUALIZER.md`: runnable viewer for the multimodal candidate in synthetic mode or on runtime traces.
- `HESIA_MISSION_MODEL.md`: mission-capable multimodal branch with low-level actuators, stage supervision, semantic/instance outputs, and training entry points.

Operational source-of-truth templates:

- `../security/systemd/hesia-drone.service`
- `../security/systemd/hesia-drone.hardening.conf`
- `../security/systemd/hesia-server.service`
- `../tools/deploy_hesia_release.sh`

Security and validation references:

- `../SECURITY_HARDENING.md`
- `../SECURITY_VALIDATION_PLAYBOOK.md`
- `../GPT.md`
