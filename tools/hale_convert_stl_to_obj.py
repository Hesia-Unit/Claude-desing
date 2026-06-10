#!/usr/bin/env python3
"""Convert generated ASCII STL HALE models to simple OBJ meshes."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "artifacts" / "hale_solar_glider" / "models"
OBJS = ROOT / "artifacts" / "hale_solar_glider" / "obj"


def parse_ascii_stl(path: Path) -> tuple[list[tuple[float, float, float]], list[tuple[int, int, int]]]:
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, int, int]] = []
    index: dict[tuple[float, float, float], int] = {}
    pending: list[int] = []

    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.strip()
        if not line.startswith("vertex "):
            continue
        _, x, y, z = line.split()
        point = (round(float(x), 6), round(float(y), 6), round(float(z), 6))
        if point not in index:
            index[point] = len(vertices) + 1
            vertices.append(point)
        pending.append(index[point])
        if len(pending) == 3:
            faces.append((pending[0], pending[1], pending[2]))
            pending = []

    return vertices, faces


def write_obj(src: Path, dst: Path) -> None:
    vertices, faces = parse_ascii_stl(src)
    lines = [
        f"# Converted from {src.name}",
        "# Concept mesh generated for HESIA HALE solar glider study",
        f"o {src.stem}",
    ]
    lines.extend(f"v {x:.6f} {y:.6f} {z:.6f}" for x, y, z in vertices)
    lines.extend(f"f {a} {b} {c}" for a, b, c in faces)
    dst.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    OBJS.mkdir(parents=True, exist_ok=True)
    count = 0
    for stl in sorted(MODELS.glob("*.stl")):
        write_obj(stl, OBJS / f"{stl.stem}.obj")
        count += 1
    print(f"converted={count} out={OBJS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
