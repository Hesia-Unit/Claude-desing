from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def _run(cmd: list[str]) -> tuple[int, str]:
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    except FileNotFoundError:
        return 127, ""
    return proc.returncode, proc.stdout


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def readelf_symbol_count(path: Path) -> int | None:
    readelf = shutil.which("readelf")
    if not readelf:
        return None
    rc, out = _run([readelf, "--wide", "--symbols", str(path)])
    if rc != 0:
        return None
    return sum(1 for line in out.splitlines() if ":" in line and "FUNC" in line)


def debuglink_name(path: Path) -> str | None:
    readelf = shutil.which("readelf")
    if not readelf:
        return None
    rc, out = _run([readelf, "--string-dump=.gnu_debuglink", str(path)])
    if rc != 0:
        return None
    for line in out.splitlines():
        if "]" not in line:
            continue
        candidate = line.split("]", 1)[-1].strip()
        if candidate:
            return candidate
    return None


def printable_strings_count(path: Path, minimum_length: int) -> int | None:
    strings = shutil.which("strings")
    if not strings:
        return None
    rc, out = _run([strings, "-a", f"-n{minimum_length}", str(path)])
    if rc != 0:
        return None
    return sum(1 for line in out.splitlines() if line.strip())


def file_description(path: Path) -> str | None:
    file_bin = shutil.which("file")
    if not file_bin:
        return None
    rc, out = _run([file_bin, str(path)])
    if rc != 0:
        return None
    return out.strip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--strings-min-len", type=int, default=6)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    artifact = args.artifact.resolve()
    if not artifact.is_file():
        raise FileNotFoundError(f"artifact not found: {artifact}")

    report = {
        "artifact": str(artifact),
        "size_bytes": artifact.stat().st_size,
        "sha256": sha256_file(artifact),
        "file": file_description(artifact),
        "function_symbol_count": readelf_symbol_count(artifact),
        "debuglink": debuglink_name(artifact),
        "printable_strings_count": printable_strings_count(artifact, args.strings_min_len),
    }

    text = json.dumps(report, indent=2)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
