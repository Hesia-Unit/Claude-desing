#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


TEXT_PATTERNS = (
    b"-----BEGIN OPENSSH PRIVATE KEY-----",
    b"-----BEGIN PRIVATE KEY-----",
    b"-----BEGIN ENCRYPTED PRIVATE KEY-----",
    b"-----BEGIN RSA PRIVATE KEY-----",
    b"-----BEGIN EC PRIVATE KEY-----",
    b"-----BEGIN DSA PRIVATE KEY-----",
)

BINARY_PATTERNS = (
    b"openssh-key-v1\x00",
)


def tracked_files(repo: Path) -> list[Path]:
    proc = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    items = [p for p in proc.stdout.split(b"\x00") if p]
    return [repo / Path(item.decode("utf-8", errors="strict")) for item in items]


def contains_private_key_marker(path: Path) -> str | None:
    try:
        data = path.read_bytes()
    except OSError as exc:
        return f"read-error:{exc}"

    for pattern in TEXT_PATTERNS:
        if pattern in data:
            return pattern.decode("ascii")
    for pattern in BINARY_PATTERNS:
        if pattern in data:
            return "openssh-key-v1"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail if tracked files contain private-key material.")
    parser.add_argument("--repo", default=".", help="Repository root")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    findings: list[tuple[Path, str]] = []
    for path in tracked_files(repo):
        if not path.is_file():
            continue
        marker = contains_private_key_marker(path)
        if marker:
            findings.append((path.relative_to(repo), marker))

    if findings:
        print("Tracked private-key material detected:", file=sys.stderr)
        for rel_path, marker in findings:
            print(f" - {rel_path} [{marker}]", file=sys.stderr)
        return 1

    print("No tracked private-key material detected.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
