#!/usr/bin/env python3
"""Run the Jetson-Ajax integration smoke test suite."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
ART = REPO / "artifacts" / "jetson_ajax"
F_WORKSPACE = Path("F:/Jetson-Ajax")


def tail(text: str, max_chars: int = 5000) -> str:
    return text[-max_chars:] if len(text) > max_chars else text


def run_step(name: str, cmd: list[str], *, timeout: int = 120, env: dict[str, str] | None = None, expect: str = "zero") -> dict[str, Any]:
    start = time.perf_counter()
    proc = subprocess.run(
        cmd,
        cwd=REPO,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )
    elapsed = time.perf_counter() - start
    output = proc.stdout + ("\n" + proc.stderr if proc.stderr else "")
    if expect == "zero":
        passed = proc.returncode == 0
    elif expect == "nonzero_certificate_required":
        passed = proc.returncode != 0 and ("certificate required" in output or "peer did not return a certificate" in output)
    else:
        raise ValueError(f"unknown expectation: {expect}")
    return {
        "name": name,
        "command": cmd,
        "returncode": proc.returncode,
        "passed": passed,
        "elapsed_sec": elapsed,
        "stdout_tail": tail(proc.stdout),
        "stderr_tail": tail(proc.stderr),
    }


def compose_env() -> dict[str, str]:
    env = os.environ.copy()
    env.update(
        {
            "JETSON_ROOTFS_HOST": r"\\wsl.localhost\Kali\mnt\f\Jetson-Ajax\mounts\rootfs",
            "HESIA_REPO_HOST": str(REPO),
            "JETSON_WORKSPACE_HOST": str(F_WORKSPACE),
            "JETSON_SYS_MIRROR_HOST": str(F_WORKSPACE / "sys_mirror"),
        }
    )
    return env


def file_checks() -> dict[str, Any]:
    required = [
        ART / "PHASE_0_INVENTORY_REPORT.pdf",
        ART / "PHASE_1_WORKSPACE_REPORT.pdf",
        ART / "PHASE_2_ROOTFS_MOUNT_REPORT.pdf",
        ART / "PHASE_3_DOCKER_SIM_REPORT.pdf",
        ART / "PHASE_4_HARDWARE_MIRROR_REPORT.pdf",
        ART / "PHASE_5_PLFM_RADAR_BRIDGE_REPORT.pdf",
        ART / "PHASE_6_IO_SECURITY_REPORT.pdf",
        ART / "PHASE_7_SERVER_MTLS_REPORT.pdf",
        ART / "PHASE_8_PIXHAWK_SITL_REPORT.pdf",
        ART / "PHASE_9_MISSION_LOOP_REPORT.pdf",
        ART / "PHASE_10_TEST_GUI_REPORT.pdf",
        ART / "phase9_mission_loop_summary.json",
        ART / "phase10_gui_server_status.json",
        F_WORKSPACE / "artifacts" / "tests" / "pixhawk_mavlink_summary_rerun.json",
    ]
    items = [{"path": str(path), "exists": path.exists(), "bytes": path.stat().st_size if path.exists() else 0} for path in required]
    return {"name": "artifact_presence", "passed": all(item["exists"] and item["bytes"] > 0 for item in items), "items": items}


def gui_check() -> dict[str, Any]:
    try:
        with urllib.request.urlopen("http://127.0.0.1:8765/api/status", timeout=5) as response:
            payload = json.loads(response.read().decode("utf-8"))
        completed = sum(1 for item in payload.get("phases", []) if item.get("status") == "completed")
        return {
            "name": "gui_api_status",
            "passed": response.status == 200 and completed >= 11,
            "http_status": response.status,
            "completed": completed,
            "phase_count": len(payload.get("phases", [])),
            "decision": ((payload.get("mission") or {}).get("decision") or {}).get("mode"),
        }
    except Exception as exc:
        return {"name": "gui_api_status", "passed": False, "error": str(exc)}


def parse_json_file(path: Path, name: str) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        return {"name": name, "passed": payload.get("status") == "passed", "payload": payload}
    except Exception as exc:
        return {"name": name, "passed": False, "error": str(exc), "path": str(path)}


def parse_radar_summary(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        summary = payload.get("summary") or {}
        passed = summary.get("status") == "ok" and int(summary.get("detection_count") or 0) > 0
        return {"name": "phase11_radar_summary", "passed": passed, "payload": payload}
    except Exception as exc:
        return {"name": "phase11_radar_summary", "passed": False, "error": str(exc), "path": str(path)}


def main() -> int:
    ART.mkdir(parents=True, exist_ok=True)
    env = compose_env()
    steps: list[dict[str, Any]] = [file_checks(), gui_check()]
    compose = ["docker", "compose", "-f", "docker/jetson_ajax/compose.yaml"]

    commands = [
        ("wsl_rootfs_mount", ["wsl", "-e", "bash", "-lc", "mount | grep '/mnt/f/Jetson-Ajax/mounts/rootfs'"], 30, "zero"),
        ("docker_image_jetson", ["docker", "image", "inspect", "hesia/jetson-ajax-sim:local"], 60, "zero"),
        ("docker_image_pixhawk", ["docker", "image", "inspect", "hesia/pixhawk-arducopter-sitl:local"], 60, "zero"),
        ("pixhawk_service_up", [*compose, "up", "-d", "pixhawk-sitl"], 120, "zero"),
        ("docker_jetson_smoke", [*compose, "run", "--rm", "jetson-sim", "/usr/local/bin/jetson-sim-smoke"], 180, "zero"),
        (
            "docker_radar_bridge",
            [
                *compose,
                "run",
                "--rm",
                "jetson-sim",
                "python3",
                "/hesia/tools/plfm_to_hesia_bridge.py",
                "--input",
                "/workspace/plfm_radar/replay/small_test_radar_data.csv",
                "--output",
                "/workspace/artifacts/tests/phase11_docker_radar.jsonl",
                "--summary",
                "/workspace/artifacts/tests/phase11_docker_radar_summary.json",
                "--threshold",
                "80",
                "--max-detections",
                "16",
            ],
            180,
            "zero",
        ),
        (
            "docker_pixhawk_bridge",
            [
                *compose,
                "run",
                "--rm",
                "jetson-sim",
                "python3",
                "/hesia/tools/pixhawk_mavlink_bridge.py",
                "--endpoint",
                "tcp:pixhawk-sitl:5760",
                "--duration-sec",
                "5",
                "--min-updates",
                "5",
                "--output",
                "/workspace/artifacts/tests/phase11_pixhawk_state.jsonl",
                "--summary",
                "/workspace/artifacts/tests/phase11_pixhawk_summary.json",
            ],
            120,
            "zero",
        ),
        (
            "host_autonomy_stack_validation",
            [
                sys.executable,
                "-m",
                "ml.autonomy_mamba2.validate_stack",
                "--device",
                "cuda",
                "--out",
                "artifacts/jetson_ajax/phase11_autonomy_stack_validation.json",
            ],
            180,
            "zero",
        ),
        (
            "server_mtls_no_client_rejected",
            [
                "wsl",
                "-e",
                "bash",
                "-lc",
                "timeout 8 openssl s_client -connect 127.0.0.1:9000 -servername ajax-desktop -verify_hostname ajax-desktop -verify_return_error -tls1_3 -CAfile /etc/hesia/certs/ca.crt < /dev/null",
            ],
            30,
            "nonzero_certificate_required",
        ),
        (
            "server_mtls_drone_client_ok",
            [
                "wsl",
                "-e",
                "bash",
                "-lc",
                "timeout 8 openssl s_client -connect 127.0.0.1:9000 -servername ajax-desktop -verify_hostname ajax-desktop -verify_return_error -tls1_3 -CAfile /etc/hesia/certs/ca.crt -cert /etc/hesia/certs/drone.crt -key /etc/hesia/certs/drone.key < /dev/null",
            ],
            30,
            "zero",
        ),
    ]

    for name, cmd, timeout, expect in commands:
        steps.append(run_step(name, cmd, timeout=timeout, env=env, expect=expect))

    steps.append(parse_json_file(F_WORKSPACE / "artifacts" / "tests" / "phase11_pixhawk_summary.json", "phase11_pixhawk_summary"))
    steps.append(parse_json_file(ART / "phase11_autonomy_stack_validation.json", "phase11_autonomy_stack_validation"))
    steps.append(parse_radar_summary(F_WORKSPACE / "artifacts" / "tests" / "phase11_docker_radar_summary.json"))

    summary = {
        "schema": "hesia.jetson_ajax.system_tests.v1",
        "status": "passed" if all(step.get("passed") for step in steps) else "failed",
        "started_at_ms": int(time.time() * 1000),
        "steps": steps,
    }
    out = ART / "phase11_system_tests.json"
    out.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps({"status": summary["status"], "steps": len(steps), "output": str(out)}, indent=2))
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
