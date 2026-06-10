#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def iter_markdown_files(root: Path):
    for path in root.rglob("*.md"):
        if any(part in {"server_source", "liboqs", "TODO", "artifacts"} for part in path.parts):
            continue
        yield path


def normalize_target(raw: str) -> str:
    target = raw.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    return target


def should_skip(target: str) -> bool:
    return (
        not target
        or target.startswith("#")
        or "://" in target
        or target.startswith("mailto:")
    )


def resolve_target(base: Path, target: str, root: Path) -> Path:
    clean = target.split("#", 1)[0]
    if clean.startswith("/"):
        return (root / clean.lstrip("/")).resolve()
    return (base.parent / clean).resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description="Check local markdown links.")
    parser.add_argument("--root", type=Path, default=Path("."))
    args = parser.parse_args()

    root = args.root.resolve()
    failures: list[str] = []

    for md in iter_markdown_files(root):
      text = md.read_text(encoding="utf-8", errors="replace")
      for lineno, line in enumerate(text.splitlines(), start=1):
        for match in LINK_RE.finditer(line):
          target = normalize_target(match.group(1))
          if should_skip(target):
            continue
          resolved = resolve_target(md, target, root)
          if not resolved.exists():
            failures.append(f"{md}:{lineno}: broken link -> {target}")

    if failures:
      for item in failures:
        print(item)
      return 1

    print("All local markdown links resolved.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
