#!/usr/bin/env python3
import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import re


SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh",
    ".py", ".sh", ".md", ".txt", ".toml", ".yml", ".yaml",
}

FIND_PACKAGE_RE = re.compile(r"find_package\s*\(\s*([A-Za-z0-9_+-]+)", re.IGNORECASE)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(65536)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def collect_dependencies(repo: Path) -> list[str]:
    deps: set[str] = set()
    for cmake_file in repo.rglob("CMakeLists.txt"):
        if "liboqs" in cmake_file.parts:
            continue
        try:
            text = cmake_file.read_text(encoding="utf-8")
        except OSError:
            continue
        deps.update(match.group(1) for match in FIND_PACKAGE_RE.finditer(text))
    return sorted(deps)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a lightweight firmware SBOM snapshot.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--output", required=True, help="Output JSON path")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    output = Path(args.output).resolve()

    components = []
    for path in sorted(repo.rglob("*")):
        if not path.is_file():
            continue
        if ".git" in path.parts:
            continue
        if path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        rel = path.relative_to(repo).as_posix()
        try:
            digest = sha256_file(path)
        except OSError:
            continue
        components.append({
            "path": rel,
            "sha256": digest,
            "type": "source" if path.suffix.lower() not in {".md", ".txt", ".toml", ".yml", ".yaml"} else "metadata",
        })

    document = {
        "format": "HESIA_FIRMWARE_SBOM_V1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "repository_root": str(repo),
        "dependencies": collect_dependencies(repo),
        "components": components,
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, sort_keys=True), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
