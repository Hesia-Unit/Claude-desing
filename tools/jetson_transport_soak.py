#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


def run_cmd(args: Sequence[str], cwd: Optional[Path] = None) -> str:
    proc = subprocess.run(
        list(args),
        cwd=str(cwd) if cwd else None,
        text=True,
        capture_output=True,
        encoding="utf-8",
        check=True,
    )
    return proc.stdout


def ssh(host: str, command: str) -> str:
    return run_cmd(["ssh", host, command])


def remote_iso_time(host: str) -> str:
    return ssh(host, "date -Iseconds").strip()


def remote_journal_since(host: str) -> str:
    # `journalctl --since` is more reliable on the target with a plain local
    # wall-clock timestamp than with an ISO-8601 timezone form.
    return ssh(host, "date '+%Y-%m-%d %H:%M:%S'").strip()


def safe_stem(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text)


def count_matches(text: str, pattern: str) -> int:
    return len(re.findall(pattern, text, flags=re.MULTILINE))


def has_server_secure_session(host: str, since_spec: str) -> bool:
    server_log = ssh(
        host,
        f"journalctl -u hesia-server.service --since '{since_spec}' --no-pager --output short-iso || true",
    )
    return bool(re.search(r"SECURE_SESSION established", server_log))


def wait_for_server_secure_session(host: str, since_spec: str, timeout_sec: int) -> bool:
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if has_server_secure_session(host, since_spec):
            return True
        time.sleep(5)
    return False


@dataclass
class Sample:
    elapsed_s: int
    remote_ts: str
    drone_active: str
    server_active: str
    drone_pid: str
    server_pid: str
    drone_secure_sessions: int
    drone_transport_failures: int
    drone_send_failures: int
    drone_queue_drops: int
    server_secure_sessions: int
    server_telemetry_ok: int
    server_video_ok: int
    server_decrypt_fail: int
    server_tls_fail: int


def gather_counts(host: str, since_spec: str) -> Tuple[Dict[str, int], Dict[str, int]]:
    drone_log = ssh(
        host,
        f"journalctl -u hesia-drone.service --since '{since_spec}' --no-pager --output short-iso || true",
    )
    server_log = ssh(
        host,
        f"journalctl -u hesia-server.service --since '{since_spec}' --no-pager --output short-iso || true",
    )
    drone_counts = {
        "secure_sessions": count_matches(drone_log, r"SESSION HESIA ÉTABLIE|SESSION HESIA ETABLIE"),
        "transport_failures": count_matches(drone_log, r"transport_write_all failed"),
        "send_failures": count_matches(drone_log, r"Send failed \(queued\)"),
        "queue_drops": count_matches(drone_log, r"Drop message \(queue full\)|Drop frame \(queue full"),
    }
    server_counts = {
        "secure_sessions": count_matches(server_log, r"SECURE_SESSION established"),
        "telemetry_ok": count_matches(server_log, r"telemetry update ok"),
        "video_ok": count_matches(server_log, r"VIDEO_DATA ok"),
        "decrypt_fail": count_matches(server_log, r"decrypt failed"),
        "tls_fail": count_matches(server_log, r"TLS handshake failed"),
    }
    return drone_counts, server_counts


def parse_timestamp_prefix(line: str) -> Optional[str]:
    match = re.match(r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})", line)
    return match.group(1) if match else None


def per_minute_counts(log_text: str, patterns: Dict[str, str]) -> Dict[str, Counter]:
    counters = {name: Counter() for name in patterns}
    for line in log_text.splitlines():
        ts = parse_timestamp_prefix(line)
        if not ts:
            continue
        minute_key = ts[:16]
        for name, pattern in patterns.items():
            if re.search(pattern, line):
                counters[name][minute_key] += 1
    return counters


def session_summary(server_log: str) -> Dict[str, Dict[str, int]]:
    sessions: Dict[str, Dict[str, int]] = defaultdict(lambda: defaultdict(int))
    for line in server_log.splitlines():
        match = re.search(r"(SERVERCPP\.[^:]+:\d+):", line)
        if not match:
            continue
        session_id = match.group(1)
        data = sessions[session_id]
        data["lines"] += 1
        if "SECURE_SESSION established" in line:
            data["secure_sessions"] += 1
        if "VIDEO_DATA ok" in line:
            data["video_ok"] += 1
        if "telemetry update ok" in line:
            data["telemetry_ok"] += 1
        if "decrypt failed" in line:
            data["decrypt_fail"] += 1
        if "TLS handshake failed" in line:
            data["tls_fail"] += 1
    return sessions


def write_report(
    output_dir: Path,
    host: str,
    duration_min: int,
    start_iso: str,
    end_iso: str,
    samples: List[Sample],
    drone_log: str,
    server_log: str,
) -> Path:
    sample_csv = output_dir / "status_samples.csv"
    with sample_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "elapsed_s",
            "remote_ts",
            "drone_active",
            "server_active",
            "drone_pid",
            "server_pid",
            "drone_secure_sessions",
            "drone_transport_failures",
            "drone_send_failures",
            "drone_queue_drops",
            "server_secure_sessions",
            "server_telemetry_ok",
            "server_video_ok",
            "server_decrypt_fail",
            "server_tls_fail",
        ])
        for sample in samples:
            writer.writerow([
                sample.elapsed_s,
                sample.remote_ts,
                sample.drone_active,
                sample.server_active,
                sample.drone_pid,
                sample.server_pid,
                sample.drone_secure_sessions,
                sample.drone_transport_failures,
                sample.drone_send_failures,
                sample.drone_queue_drops,
                sample.server_secure_sessions,
                sample.server_telemetry_ok,
                sample.server_video_ok,
                sample.server_decrypt_fail,
                sample.server_tls_fail,
            ])

    drone_patterns = {
        "transport_failures": r"transport_write_all failed",
        "send_failures": r"Send failed \(queued\)",
        "queue_drops": r"Drop message \(queue full\)|Drop frame \(queue full",
        "secure_sessions": r"SESSION HESIA ÉTABLIE|SESSION HESIA ETABLIE",
    }
    server_patterns = {
        "secure_sessions": r"SECURE_SESSION established",
        "telemetry_ok": r"telemetry update ok",
        "video_ok": r"VIDEO_DATA ok",
        "decrypt_fail": r"decrypt failed",
        "tls_fail": r"TLS handshake failed",
    }
    drone_minute = per_minute_counts(drone_log, drone_patterns)
    server_minute = per_minute_counts(server_log, server_patterns)
    sessions = session_summary(server_log)

    summary = {
        "host": host,
        "duration_min": duration_min,
        "start_iso": start_iso,
        "end_iso": end_iso,
        "sample_count": len(samples),
        "drone_active_all_samples": all(s.drone_active == "active" for s in samples),
        "server_active_all_samples": all(s.server_active == "active" for s in samples),
        "final_drone_counts": {
            "secure_sessions": count_matches(drone_log, drone_patterns["secure_sessions"]),
            "transport_failures": count_matches(drone_log, drone_patterns["transport_failures"]),
            "send_failures": count_matches(drone_log, drone_patterns["send_failures"]),
            "queue_drops": count_matches(drone_log, drone_patterns["queue_drops"]),
        },
        "final_server_counts": {
            "secure_sessions": count_matches(server_log, server_patterns["secure_sessions"]),
            "telemetry_ok": count_matches(server_log, server_patterns["telemetry_ok"]),
            "video_ok": count_matches(server_log, server_patterns["video_ok"]),
            "decrypt_fail": count_matches(server_log, server_patterns["decrypt_fail"]),
            "tls_fail": count_matches(server_log, server_patterns["tls_fail"]),
        },
        "sessions": sessions,
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    report = output_dir / "report.md"
    lines: List[str] = []
    lines.append("# HESIA Jetson Transport Soak Report")
    lines.append("")
    lines.append("## Scope")
    lines.append("")
    lines.append(f"- Host: `{host}`")
    lines.append(f"- Duration: `{duration_min}` minutes")
    lines.append(f"- Start (remote): `{start_iso}`")
    lines.append(f"- End (remote): `{end_iso}`")
    lines.append("- Method: service-state sampling plus full `journalctl` collection for drone and server")
    lines.append("")
    lines.append("## Service Stability")
    lines.append("")
    lines.append(f"- Drone active on all samples: `{summary['drone_active_all_samples']}`")
    lines.append(f"- Server active on all samples: `{summary['server_active_all_samples']}`")
    lines.append(f"- Samples collected: `{len(samples)}`")
    lines.append("")
    lines.append("## Final Aggregate Counts")
    lines.append("")
    lines.append("### Drone")
    lines.append("")
    for key, value in summary["final_drone_counts"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.append("")
    lines.append("### Server")
    lines.append("")
    for key, value in summary["final_server_counts"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.append("")
    lines.append("## Session-Level Correlation")
    lines.append("")
    if sessions:
        for session_id, data in sorted(sessions.items()):
            lines.append(f"- `{session_id}`")
            lines.append(f"  - `secure_sessions={data.get('secure_sessions', 0)}`")
            lines.append(f"  - `video_ok={data.get('video_ok', 0)}`")
            lines.append(f"  - `telemetry_ok={data.get('telemetry_ok', 0)}`")
            lines.append(f"  - `decrypt_fail={data.get('decrypt_fail', 0)}`")
            lines.append(f"  - `tls_fail={data.get('tls_fail', 0)}`")
    else:
        lines.append("- No session markers found in server log.")
    lines.append("")
    lines.append("## Per-Minute Correlation")
    lines.append("")
    all_minutes = sorted(
        set().union(
            *[set(counter.keys()) for counter in drone_minute.values()],
            *[set(counter.keys()) for counter in server_minute.values()],
        )
    )
    if all_minutes:
        lines.append("| Minute | Drone secure | Drone transport fail | Drone send fail | Drone queue drops | Server secure | Server telemetry | Server video | Server decrypt fail | Server TLS fail |")
        lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
        for minute in all_minutes:
            lines.append(
                f"| {minute} "
                f"| {drone_minute['secure_sessions'][minute]} "
                f"| {drone_minute['transport_failures'][minute]} "
                f"| {drone_minute['send_failures'][minute]} "
                f"| {drone_minute['queue_drops'][minute]} "
                f"| {server_minute['secure_sessions'][minute]} "
                f"| {server_minute['telemetry_ok'][minute]} "
                f"| {server_minute['video_ok'][minute]} "
                f"| {server_minute['decrypt_fail'][minute]} "
                f"| {server_minute['tls_fail'][minute]} |"
            )
    else:
        lines.append("- No minute-level events captured.")
    lines.append("")
    lines.append("## Raw Artifacts")
    lines.append("")
    lines.append("- `status_samples.csv`")
    lines.append("- `drone_journal.log`")
    lines.append("- `server_journal.log`")
    lines.append("- `summary.json`")
    lines.append("")
    lines.append("## Analyst Notes")
    lines.append("")
    lines.append("- Read `summary.json` for machine-readable totals.")
    lines.append("- Use the minute table to correlate server continuity versus drone-side transport noise.")
    lines.append("- If server `VIDEO_DATA ok` continues while the drone reports transport failures, inspect transient write handling and log suppression rather than assuming immediate session loss.")
    lines.append("")

    report.write_text("\n".join(lines), encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Run and analyze a Jetson HESIA transport soak test over SSH.")
    parser.add_argument("--host", default="ajax-desktop")
    parser.add_argument("--duration-min", type=int, default=60)
    parser.add_argument("--interval-sec", type=int, default=60)
    parser.add_argument("--wait-for-server-session", action="store_true")
    parser.add_argument("--wait-session-timeout-sec", type=int, default=180)
    parser.add_argument("--output-root", type=Path, default=Path("artifacts") / "jetson_transport_soak")
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    output_dir = args.output_root / stamp
    output_dir.mkdir(parents=True, exist_ok=True)

    start_iso = remote_iso_time(args.host)
    start_since = remote_journal_since(args.host)
    if args.wait_for_server_session:
        if not wait_for_server_secure_session(args.host, start_since, args.wait_session_timeout_sec):
            raise SystemExit(
                f"Timed out after {args.wait_session_timeout_sec}s waiting for a fresh "
                f"server secure session on {args.host}"
            )
    samples: List[Sample] = []
    sample_count = max(1, int((args.duration_min * 60) / max(1, args.interval_sec)))

    for idx in range(sample_count):
        remote_ts = remote_iso_time(args.host)
        drone_active = ssh(args.host, "systemctl is-active hesia-drone.service || true").strip()
        server_active = ssh(args.host, "systemctl is-active hesia-server.service || true").strip()
        drone_pid = ssh(args.host, "systemctl show -p MainPID --value hesia-drone.service || true").strip()
        server_pid = ssh(args.host, "systemctl show -p MainPID --value hesia-server.service || true").strip()
        drone_counts, server_counts = gather_counts(args.host, start_since)
        samples.append(
            Sample(
                elapsed_s=idx * args.interval_sec,
                remote_ts=remote_ts,
                drone_active=drone_active,
                server_active=server_active,
                drone_pid=drone_pid,
                server_pid=server_pid,
                drone_secure_sessions=drone_counts["secure_sessions"],
                drone_transport_failures=drone_counts["transport_failures"],
                drone_send_failures=drone_counts["send_failures"],
                drone_queue_drops=drone_counts["queue_drops"],
                server_secure_sessions=server_counts["secure_sessions"],
                server_telemetry_ok=server_counts["telemetry_ok"],
                server_video_ok=server_counts["video_ok"],
                server_decrypt_fail=server_counts["decrypt_fail"],
                server_tls_fail=server_counts["tls_fail"],
            )
        )
        if idx < sample_count - 1:
            time.sleep(args.interval_sec)

    end_iso = ssh(args.host, "date -Iseconds").strip()
    drone_log = ssh(
        args.host,
        f"journalctl -u hesia-drone.service --since '{start_since}' --no-pager --output short-iso || true",
    )
    server_log = ssh(
        args.host,
        f"journalctl -u hesia-server.service --since '{start_since}' --no-pager --output short-iso || true",
    )
    (output_dir / "drone_journal.log").write_text(drone_log, encoding="utf-8")
    (output_dir / "server_journal.log").write_text(server_log, encoding="utf-8")
    report_path = write_report(output_dir, args.host, args.duration_min, start_iso, end_iso, samples, drone_log, server_log)

    print(json.dumps({
        "output_dir": str(output_dir),
        "report": str(report_path),
        "samples": len(samples),
        "start_iso": start_iso,
        "end_iso": end_iso,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
