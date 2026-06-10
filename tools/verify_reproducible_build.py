#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Build the same targets twice in clean trees and compare hashes.")
    parser.add_argument("--repo", default=".", help="Repository root")
    parser.add_argument("--build-script", required=True, help="Relative path to the build script inside the repo")
    parser.add_argument("--artifact", action="append", required=True, help="Relative artifact path to compare")
    parser.add_argument("--exclude", action="append", default=[], help="Top-level repo entry to exclude from the clean copies")
    parser.add_argument("--output", required=True, help="Output JSON report")
    return parser.parse_args()


def ignore_copy(src, names, extra_ignored):
    ignored = {".git", ".github", "build", "artifacts", "__pycache__"}
    ignored.update(extra_ignored)
    return [name for name in names if name in ignored]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(65536)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def run_build(copy_root: Path, script_rel: str, epoch: str):
    env = os.environ.copy()
    env["TZ"] = "UTC"
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    env["SOURCE_DATE_EPOCH"] = epoch
    subprocess.run(["bash", script_rel], cwd=copy_root, env=env, check=True)


def collect_manifest(copy_root: Path, artifacts):
    manifest = []
    for rel in artifacts:
        path = copy_root / rel
        if not path.exists():
            raise FileNotFoundError(f"Missing artifact: {rel}")
        manifest.append({
            "path": rel,
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        })
    return manifest


def main():
    args = parse_args()
    repo = Path(args.repo).resolve()
    output = Path(args.output).resolve()
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "1704067200")
    extra_ignored = set(args.exclude)

    with tempfile.TemporaryDirectory(prefix="hesia-repro-") as tmp_dir:
        tmp_root = Path(tmp_dir)
        copy_a = tmp_root / "copy_a"
        copy_b = tmp_root / "copy_b"
        ignore = lambda src, names: ignore_copy(src, names, extra_ignored)
        shutil.copytree(repo, copy_a, ignore=ignore)
        shutil.copytree(repo, copy_b, ignore=ignore)

        run_build(copy_a, args.build_script, epoch)
        run_build(copy_b, args.build_script, epoch)

        manifest_a = collect_manifest(copy_a, args.artifact)
        manifest_b = collect_manifest(copy_b, args.artifact)

    identical = manifest_a == manifest_b
    report = {
        "format": "HESIA_REPRO_BUILD_V1",
        "repository_root": str(repo),
        "source_date_epoch": epoch,
        "artifacts": manifest_a,
        "identical": identical,
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not identical:
        raise SystemExit("Reproducible-build verification failed")


if __name__ == "__main__":
    main()
