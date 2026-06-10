#!/usr/bin/env python3
import argparse
from pathlib import Path
import sys


SUSPICIOUS_EXTENSIONS = {
    ".a", ".bin", ".dll", ".dylib", ".engine", ".exe", ".lib", ".mp4",
    ".o", ".obj", ".pyd", ".so",
}

IGNORED_DIRS = {
    ".git", ".github", "__pycache__", "build", "dist",
}


def read_allowlist(path: Path) -> set[str]:
    if not path.exists():
        return set()
    return {
        line.strip().replace("\\", "/")
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.strip().startswith("#")
    }


def looks_binary(path: Path) -> bool:
    if path.suffix.lower() in SUSPICIOUS_EXTENSIONS:
        return True
    try:
        sample = path.read_bytes()[:4096]
    except OSError:
        return False
    return b"\x00" in sample


def main() -> int:
    parser = argparse.ArgumentParser(description="Reject opaque binaries and nested VCS metadata.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--allowlist", default="security/allowed-binaries.txt", help="Allowed binary list")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    allowlist = read_allowlist((repo / args.allowlist).resolve())

    nested_git = []
    opaque_files = []

    for path in repo.rglob("*"):
        rel = path.relative_to(repo).as_posix()

        if path.is_dir():
            if path.name == ".git" and path != repo / ".git":
                nested_git.append(rel)
            continue

        if any(part in IGNORED_DIRS for part in path.parts[:-1]):
            continue

        if looks_binary(path) and rel not in allowlist:
            opaque_files.append(rel)

    if nested_git:
        print("Nested VCS metadata is forbidden:", file=sys.stderr)
        for item in nested_git:
            print(f"  - {item}", file=sys.stderr)

    if opaque_files:
        print("Opaque binary artifacts detected outside the allowlist:", file=sys.stderr)
        for item in opaque_files:
            print(f"  - {item}", file=sys.stderr)

    return 1 if nested_git or opaque_files else 0


if __name__ == "__main__":
    raise SystemExit(main())
