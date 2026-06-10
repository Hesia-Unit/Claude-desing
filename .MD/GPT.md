# GPT Worklog And Execution Plan

## Warning

Do not stop before all executable work for this phase is completed or a hard external blocker is proven.
Every claim must be backed by logs, files, measurements, builds, or direct remote inspection.
Do not emulate missing security features. If a platform feature is unavailable, document the real limitation and adapt policy explicitly for this Jetson-only version.
Continuous execution mode is active: keep progressing autonomously across remaining objectives until the user explicitly stops the run.

## Mission

Phase objectives, in order:

1. Prove the drone executable runs on Jetson and connects to the server.
2. Remove the RPMB requirement for the Jetson Orin Nano Super version because RPMB is unavailable on this target.
3. Harden and obfuscate the binary with the best realistic compromise between reverse-engineering cost, runtime stability, and performance.
4. Produce exhaustive world-class engineering documentation covering installation, provisioning, operation, maintenance, recovery, update flow, security assumptions, OP-TEE/TA behavior, algorithms, and troubleshooting.
5. Perform ML R&D for a new embedded multimodal model replacing YOLO + Mamba while keeping MiDaS.
6. Use Arkveld for training and experiments.
7. Build a simulator to train the multimodal control model for drone piloting.
8. Write professional technical papers/reports for the completed work.

## Non-Negotiables

- No invented results.
- No fake HSM, no fake RPMB, no fake benchmarks.
- Every remote action must be reproducible from logs or scripts.
- Preserve the existing cryptographic families already chosen by the project.
- Prefer fail-closed behavior in production paths.

## Remote Targets

### Jetson

- Hostname: `ajax-desktop`
- Tailscale IP: `100.101.152.53`
- User: `ajax`
- Project root: `/home/ajax/.cache/.hesia`
- Sudo password available in task context

Primary goals on Jetson:

- Build and run `hesia_drone`
- Verify OP-TEE / TA runtime
- Verify service hardening and sandboxing
- Prove drone/server connectivity
- Measure obfuscation and runtime impact

### Arkveld

- Hostname: `arkveld`
- Tailscale IP: `100.98.193.17`
- SSH key expected under `C:\Users\matis\.ssh`

Primary goals on Arkveld:

- Prepare ML environment
- Run research and experiments
- Train candidate models
- Run simulation training workloads

## Execution Order

### Track A: Runtime And Security

1. Confirm access to Jetson and Arkveld.
2. Create a clean runtime checkpoint and evidence log.
3. Patch policy/config so Jetson version does not require RPMB.
4. Start/repair server.
5. Start/repair drone.
6. Capture proof of successful connection.
7. Harden binary and deployment.
8. Validate sandboxing and service hardening after changes.

### Track B: Documentation

1. Inventory all firmware, TA, server, provisioning, and maintenance flows.
2. Document installation from bare machine.
3. Document keys, certificates, secure storage, policy, release, rollback, and recovery flows.
4. Document TA commands, OP-TEE session authentication, ML-DSA path, measured boot, asset manifest, and A/B update flow.
5. Document day-2 operations, debugging, alerts, rotation, recovery, and incident handling.

### Track C: ML R&D

1. Research recent multimodal embedded architectures from primary sources.
2. Formulate target requirements for Jetson inference.
3. Design a custom architecture for embedded robotics.
4. Integrate a lottery-ticket / iterative pruning path so the final deployed model is a sparse, low-cost subnet rather than only a dense baseline.
5. Implement training/inference scaffolding.
6. Train and compare candidate models on Arkveld.
7. Measure pruning schedules, winning-ticket stability, and Jetson deployment trade-offs.
6. Build simulation loop for policy training.
7. Write papers and technical reports with real results only.

## Evidence Checklist

- `git diff` for each meaningful change set
- Jetson build logs
- Jetson service logs
- Server logs
- Proof of drone/server handshake
- Sandbox runtime proof: `Seccomp`, `NoNewPrivs`, service hardening
- Binary size and performance deltas before/after obfuscation
- Documentation files produced
- ML experiment configs, metrics, hardware inventory, and training logs
- Simulator run outputs and evaluation traces

## Known Facts At Start Of Phase

- The TA-backed ML-DSA path works on Jetson.
- The old P-256 attestation path was removed from the critical runtime path because it crashed.
- The current Jetson boot medium is SD, not eMMC/MMC with RPMB.
- The service already shows active seccomp and `NoNewPrivs=1`.
- The server service was inactive at phase start and must be checked/restarted.

## Open Questions To Resolve

- Exact server executable/service state on Jetson or elsewhere.
- Exact Arkveld SSH identity path and known-host status.
- Best practical binary obfuscation pipeline for this codebase and target toolchain.
- Best multimodal embedded architecture after literature review and initial profiling.
- Whether the final embedded model should keep sparsity as a training artifact only or also target a runtime representation that preserves the lottery-ticket gains on Jetson.

## Deliverables

- Running Jetson drone connected to server
- Updated policy/config for Jetson no-RPMB variant
- Hardened and obfuscated binary build path
- Full engineering documentation set
- ML R&D code, experiments, and results
- Simulator and training loop
- Final technical papers/reports
