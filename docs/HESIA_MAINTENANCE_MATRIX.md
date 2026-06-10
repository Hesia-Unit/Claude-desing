# HESIA Maintenance Matrix

## Purpose

This is the short-form operational matrix for responders who need a direct mapping from symptom to action.

Use it together with:

- [HESIA_OPERATIONS_RUNBOOK.md](/C:/Users/matis/Documents/Hesia-Firmware/docs/HESIA_OPERATIONS_RUNBOOK.md)
- [HESIA_TA_OPTEE_REFERENCE.md](/C:/Users/matis/Documents/Hesia-Firmware/docs/HESIA_TA_OPTEE_REFERENCE.md)
- [HESIA_INSTALLATION_GUIDE.md](/C:/Users/matis/Documents/Hesia-Firmware/docs/HESIA_INSTALLATION_GUIDE.md)

## 1. Service-level symptoms

| Symptom | First check | Likely cause | Primary action |
| --- | --- | --- | --- |
| `hesia-server.service` inactive | `systemctl status hesia-server.service` | bad release deploy, key load failure, policy failure | inspect server journal, confirm `/opt/hesia/bin/hesia_server_cpp`, confirm secure material paths |
| `hesia-drone.service` crash loop | `journalctl -u hesia-drone.service -n 120` | policy rejection, OP-TEE auth failure, allowlist mismatch, video source error | read journal first, then inspect policy and `/etc/hesia/secure` |
| drone runs but no secure session | latest `SERVERCPP.*.log` | transport or identity verification failure | confirm server listening, pinned keys, allowlist, and policy signatures |

## 2. Trust-chain symptoms

| Symptom | First check | Likely cause | Primary action |
| --- | --- | --- | --- |
| `Firmware hash not in allowlist` | `/etc/hesia/secure/firmware_allowlist.txt` and `/etc/hesia/secure/firmware_allowlist.txt.sig` | rebuilt drone binary not re-hashed or detached signature written to the wrong filename | run `tools/refresh_firmware_allowlist.sh /opt/hesia/bin/hesia_drone` |
| policy signature failure | `/etc/hesia/policy/` set | stale or mismatched signature bundle | redeploy `policy.conf`, `policy.sig`, `policy.sig.pqc` together |
| `SERVER_AUTH` verification failure | server public key and session logs | mismatched server ML-DSA identity | confirm active TEE-backed server slot, export and redeploy matching public key if needed |

## 3. OP-TEE symptoms

| Symptom | First check | Likely cause | Primary action |
| --- | --- | --- | --- |
| `optee_session_auth.sealed` missing | `/etc/hesia/secure/optee_session_auth.sealed` | bootstrap not completed | run the provisioning script with the host tool |
| TA public anchor mismatch | `/etc/hesia/secure/*.bin` | rotation performed without re-export or without server refresh | re-export TA anchors and copy fresh public material to the verifier side |
| TEE signing required but unavailable | policy + startup log | TA slot not provisioned or backend unavailable | import or generate the slot key, then restart and revalidate |

## 4. Video / perception symptoms

| Symptom | First check | Likely cause | Primary action |
| --- | --- | --- | --- |
| video stalls after session start | latest session log and drone journal | queue pressure, invalid source, file replay EOF | inspect `drone.video_send_queue_max`, `drone.video_min_send_interval_ms`, `HESIA_VIDEO_SOURCE` |
| no video source at startup | journal | missing camera or disallowed file replay | provide a real camera or set `HESIA_ALLOW_FILE_VIDEO_SOURCE=1` explicitly for replay |
| pipeline noisy but still alive | drone journal | overly verbose callback or worker logs | reduce log verbosity and confirm throughput before changing queueing logic |
| M2B Jetson benchmark cannot build engine | `artifacts/arkveld_ml/*.onnx` and Jetson Python modules | ONNX exported with hidden external data or missing TensorRT runtime deps | re-export monolithic ONNX, confirm `python3 -c "import tensorrt, numpy"`, rerun `tools/jetson_benchmark_m2b.sh` |

## 5. Release maintenance tasks

### Replace a release binary

1. stop the corresponding service
2. clear immutable bit
3. install the new binary into `/opt/hesia/bin`
4. restore owner, group, and mode
5. restore immutable bit
6. refresh allowlist if the drone binary changed
7. restart the service
8. validate runtime and latest secure session

### Rotate drone identity

1. back up `/etc/hesia/secure`
2. rotate session authentication if needed
3. rotate drone TA identity
4. export fresh public anchors
5. copy public anchors to the server
6. restart
7. verify handshake

### Rotate server identity

1. rotate the server slot or runtime key set
2. refresh deployed public key on verifiers
3. restart the server
4. force a new drone connection
5. verify `SERVER_AUTH` again

## 6. Immutable-file handling

Files often protected with `chattr +i`:

- `/opt/hesia/bin/hesia_drone`
- `/opt/hesia/bin/hesia_server_cpp`
- `/etc/hesia/secure/firmware_allowlist.txt`
- sealed runtime blobs in `/etc/hesia/secure`

Maintenance rule:

- clear immutable only for the shortest possible window
- restore it immediately after the write

## 7. Validation commands

Minimum operator set:

```bash
systemctl --no-pager --full status hesia-server.service
systemctl --no-pager --full status hesia-drone.service
pid=$(systemctl show -p MainPID --value hesia-drone.service)
egrep '^(Name|NoNewPrivs|Seccomp):' \"/proc/${pid}/status\"
f=$(ls -1t /var/log/hesia/drone/SERVERCPP.*.log | head -n 1)
tail -n 120 \"$f\"
```

## 8. Escalate instead of guessing

Stop and escalate when:

- a TA anchor changed unexpectedly
- a secure-session failure appears after a key rotation
- the deployed binary hash and allowlist hash disagree more than once in a row
- a service runs only from the build tree and not from `/opt/hesia/bin`
