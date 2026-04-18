#!/usr/bin/env bash
set -euo pipefail

PUB=""
OUT=""

usage() {
  cat <<'USAGE'
Usage:
  hesia-policy-ed25519-embed.sh --pub <policy_pub.pem> --emit-cpp <out.cpp>

Example:
  ./tools/hesia-policy-ed25519-embed.sh \
    --pub /mnt/c/Users/matis/Documents/Hesia/Hesia-Simulation/_exports/policy_pub.pem \
    --emit-cpp /mnt/c/Users/matis/Documents/Hesia/Hesia-Simulation/AIR/IMPLEMENTATION/Jetson/GPT/drone/policy_ed25519_public_key.cpp
USAGE
}

while [ $# -gt 0 ]; do
  case "$1" in
    --pub)
      PUB="$2"
      shift 2
      ;;
    --emit-cpp)
      OUT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ -z "$PUB" ] || [ -z "$OUT" ]; then
  usage >&2
  exit 2
fi

python3 - "$PUB" "$OUT" <<'PY'
import sys
from pathlib import Path

pub_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])

data = pub_path.read_bytes()

lines = []
lines.append('#include "policy_ed25519_public_key.h"')
lines.append("")
lines.append("namespace hesia {")
lines.append("")
lines.append("const std::uint8_t kPolicyEd25519PublicKeyPem[] = {")
if data:
    for i, b in enumerate(data):
        if i % 12 == 0:
            lines.append("    " + ", ".join(f"0x{bb:02x}" for bb in data[i:i+12]) + ",")
    # Remove trailing comma on last line
    if lines[-1].endswith(","):
        lines[-1] = lines[-1].rstrip(",")
lines.append("};")
lines.append("const std::size_t kPolicyEd25519PublicKeyPemLen = sizeof(kPolicyEd25519PublicKeyPem);")
lines.append("")
lines.append("} // namespace hesia")
lines.append("")

out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
print(f"[ed25519-embed] Wrote {out_path} ({len(data)} bytes)")
PY
