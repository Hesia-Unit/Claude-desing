#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional


SERVER_LINE_RE = re.compile(r"^(?P<ts>\S+)\s+.*SERVERCPP\.(?P<session>[^:]+:\d+):\s(?P<msg>.*)$")
TS_RE = re.compile(r"^(?P<ts>\S+)\s+")
TS_FORMAT = "%Y-%m-%dT%H:%M:%S%z"


@dataclass
class SessionMetrics:
    first_ts: Optional[str] = None
    last_ts: Optional[str] = None
    secure_sessions: int = 0
    telemetry_ok: int = 0
    video_ok: int = 0
    session_errors: int = 0
    decrypt_failures: int = 0
    tls_failures: int = 0
    max_video_total: int = 0


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="replace")


def parse_ts(raw: str) -> datetime:
    return datetime.strptime(raw, TS_FORMAT)


def load_status_samples(path: Path) -> List[Dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def analyze_server_log(lines: List[str]) -> Dict[str, SessionMetrics]:
    sessions: Dict[str, SessionMetrics] = defaultdict(SessionMetrics)
    for line in lines:
        match = SERVER_LINE_RE.match(line)
        if not match:
            continue
        session_id = match.group("session")
        ts = match.group("ts")
        msg = match.group("msg")
        metrics = sessions[session_id]
        if metrics.first_ts is None:
            metrics.first_ts = ts
        metrics.last_ts = ts
        if "SECURE_SESSION established" in msg:
            metrics.secure_sessions += 1
        if "CONST telemetry update ok" in msg:
            metrics.telemetry_ok += 1
        if "VIDEO_DATA ok" in msg:
            metrics.video_ok += 1
            total_match = re.search(r"total=(\d+)", msg)
            if total_match:
                metrics.max_video_total = max(metrics.max_video_total, int(total_match.group(1)))
        if "Session error" in msg:
            metrics.session_errors += 1
        if "decrypt failed" in msg:
            metrics.decrypt_failures += 1
        if "TLS handshake failed" in msg:
            metrics.tls_failures += 1
    return dict(sessions)


def collect_timestamps(lines: List[str], patterns: List[str]) -> Dict[str, List[str]]:
    results: Dict[str, List[str]] = {pattern: [] for pattern in patterns}
    compiled = {pattern: re.compile(pattern) for pattern in patterns}
    for line in lines:
        ts_match = TS_RE.match(line)
        if not ts_match:
            continue
        ts = ts_match.group("ts")
        for pattern, regex in compiled.items():
            if regex.search(line):
                results[pattern].append(ts)
    return results


def choose_primary_session(sessions: Dict[str, SessionMetrics]) -> Optional[str]:
    if not sessions:
        return None
    return max(
        sessions.items(),
        key=lambda item: (
            item[1].secure_sessions,
            item[1].telemetry_ok + item[1].video_ok,
            item[1].max_video_total,
        ),
    )[0]


def build_summary(artifact_dir: Path) -> Dict[str, object]:
    server_lines = read_text(artifact_dir / "server_journal.log").splitlines()
    drone_lines = read_text(artifact_dir / "drone_journal.log").splitlines()
    samples = load_status_samples(artifact_dir / "status_samples.csv")

    sessions = analyze_server_log(server_lines)
    primary_session = choose_primary_session(sessions)

    drone_events = collect_timestamps(
        drone_lines,
        [
            r"Replay video boucle - iteration #\d+",
            r"Fin du flux vidéo|pas de frame",
            r"transport_write_all failed",
            r"repli sur cle epinglee",
            r"get_recovery_challenge",
        ],
    )

    summary: Dict[str, object] = {
        "artifact_dir": str(artifact_dir),
        "sample_count": len(samples),
        "drone_active_all_samples": all(sample.get("drone_active") == "active" for sample in samples) if samples else None,
        "server_active_all_samples": all(sample.get("server_active") == "active" for sample in samples) if samples else None,
        "primary_session": primary_session,
        "sessions": {
            session_id: {
                "first_ts": metrics.first_ts,
                "last_ts": metrics.last_ts,
                "secure_sessions": metrics.secure_sessions,
                "telemetry_ok": metrics.telemetry_ok,
                "video_ok": metrics.video_ok,
                "session_errors": metrics.session_errors,
                "decrypt_failures": metrics.decrypt_failures,
                "tls_failures": metrics.tls_failures,
                "max_video_total": metrics.max_video_total,
            }
            for session_id, metrics in sessions.items()
        },
        "drone_events": {
            "loop_iterations": drone_events[r"Replay video boucle - iteration #\d+"],
            "video_eof_events": drone_events[r"Fin du flux vidéo|pas de frame"],
            "transport_failures": drone_events[r"transport_write_all failed"],
            "pinned_pubkey_fallbacks": drone_events[r"repli sur cle epinglee"],
            "recovery_challenge_errors": drone_events[r"get_recovery_challenge"],
        },
    }

    if primary_session:
        session = sessions[primary_session]
        if session.first_ts and session.last_ts:
            summary["primary_session_duration_sec"] = int(
                (parse_ts(session.last_ts) - parse_ts(session.first_ts)).total_seconds()
            )
    return summary


def build_report(summary: Dict[str, object]) -> str:
    lines: List[str] = []
    lines.append("# HESIA Jetson Transport Artifact Analysis")
    lines.append("")
    lines.append(f"- Artifact: `{summary['artifact_dir']}`")
    if summary.get("sample_count") is not None:
        lines.append(f"- Samples: `{summary['sample_count']}`")
    if summary.get("drone_active_all_samples") is not None:
        lines.append(f"- Drone active on all samples: `{summary['drone_active_all_samples']}`")
    if summary.get("server_active_all_samples") is not None:
        lines.append(f"- Server active on all samples: `{summary['server_active_all_samples']}`")
    lines.append("")

    primary_session = summary.get("primary_session")
    if primary_session:
        session = summary["sessions"][primary_session]
        lines.append("## Primary Session")
        lines.append("")
        lines.append(f"- Session: `{primary_session}`")
        lines.append(f"- First log: `{session['first_ts']}`")
        lines.append(f"- Last log: `{session['last_ts']}`")
        if summary.get("primary_session_duration_sec") is not None:
            lines.append(f"- Duration covered by logs: `{summary['primary_session_duration_sec']}` seconds")
        lines.append(f"- Secure session markers: `{session['secure_sessions']}`")
        lines.append(f"- Telemetry OK lines: `{session['telemetry_ok']}`")
        lines.append(f"- Video OK lines: `{session['video_ok']}`")
        lines.append(f"- Max `VIDEO_DATA` total: `{session['max_video_total']}`")
        lines.append(f"- Session errors: `{session['session_errors']}`")
        lines.append(f"- Decrypt failures: `{session['decrypt_failures']}`")
        lines.append(f"- TLS failures: `{session['tls_failures']}`")
        lines.append("")

    drone_events = summary["drone_events"]
    lines.append("## Drone-Side Events")
    lines.append("")
    lines.append(f"- Replay iterations: `{len(drone_events['loop_iterations'])}`")
    if drone_events["loop_iterations"]:
        lines.append("- Replay timestamps:")
        for ts in drone_events["loop_iterations"]:
            lines.append(f"  - `{ts}`")
    lines.append(f"- EOF events: `{len(drone_events['video_eof_events'])}`")
    lines.append(f"- Transport write failures: `{len(drone_events['transport_failures'])}`")
    lines.append(f"- Pinned attestation-pubkey fallback warnings: `{len(drone_events['pinned_pubkey_fallbacks'])}`")
    lines.append(f"- Recovery-challenge errors observed in logs: `{len(drone_events['recovery_challenge_errors'])}`")
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze a Jetson soak artifact directory.")
    parser.add_argument("artifact_dir", type=Path)
    args = parser.parse_args()

    artifact_dir = args.artifact_dir.resolve()
    summary = build_summary(artifact_dir)
    (artifact_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    (artifact_dir / "report.md").write_text(build_report(summary), encoding="utf-8")
    print(json.dumps({"artifact_dir": str(artifact_dir), "summary": str(artifact_dir / "summary.json")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
