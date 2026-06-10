#!/usr/bin/env python3
import argparse
import re
from pathlib import Path
import sys


FIND_PACKAGE_RE = re.compile(r"find_package\s*\(\s*([A-Za-z0-9_+-]+)", re.IGNORECASE)
FIND_LIBRARY_RE = re.compile(r"find_library\s*\(\s*([A-Za-z0-9_+-]+)", re.IGNORECASE)


def load_allowlist(path: Path) -> set[str]:
    if not path.exists():
        return set()
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.strip().startswith("#")
    }


def normalize_dependency(name: str) -> str:
    mapping = {
        "CUDA_CUDART_LIBRARY": "CUDA",
        "GNARL_LIBRARY": "GNAT",
        "GNAT_LIBRARY": "GNAT",
        "LIBOQS_LIBRARY": "liboqs",
        "SECCOMP_LIBRARY": "seccomp",
        "TEEC_LIBRARY": "teec",
        "TENSORRT_NVINFER_LIBRARY": "TensorRT",
        "TENSORRT_PARSER_LIBRARY": "TensorRT",
    }
    return mapping.get(name, name)


def collect_dependencies(repo: Path) -> set[str]:
    deps: set[str] = set()
    for cmake_file in repo.rglob("CMakeLists.txt"):
        if "liboqs" in cmake_file.parts:
            continue
        try:
            text = cmake_file.read_text(encoding="utf-8")
        except OSError:
            continue
        deps.update(normalize_dependency(match.group(1)) for match in FIND_PACKAGE_RE.finditer(text))
        deps.update(normalize_dependency(match.group(1)) for match in FIND_LIBRARY_RE.finditer(text))
    return deps


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify declared build dependencies against an allowlist.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--allowlist", default="security/approved-dependencies.txt", help="Approved dependency list")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    approved = load_allowlist((repo / args.allowlist).resolve())
    declared = collect_dependencies(repo)

    unexpected = sorted(dep for dep in declared if dep not in approved)
    missing = sorted(dep for dep in approved if dep not in declared)

    if unexpected:
        print("Unexpected declared dependencies:", file=sys.stderr)
        for dep in unexpected:
            print(f"  - {dep}", file=sys.stderr)

    if missing:
        print("Approved dependencies not currently declared:", file=sys.stderr)
        for dep in missing:
            print(f"  - {dep}", file=sys.stderr)

    return 1 if unexpected or missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
