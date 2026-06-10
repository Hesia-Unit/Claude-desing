#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
python3 "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check_doc_links.py" --root "$ROOT"
