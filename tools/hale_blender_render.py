"""Render selected HALE STL models in Blender.

Usage:
  blender.exe -b --python tools/hale_blender_render.py -- --selected artifacts/hale_solar_glider/hale_selected.json --models artifacts/hale_solar_glider/models --out artifacts/hale_solar_glider/renders
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--selected", type=Path, required=True)
    parser.add_argument("--models", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--width", type=int, default=1400)
    parser.add_argument("--height", type=int, default=900)
    return parser.parse_args(argv)


def reset_scene(width: int, height: int) -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    scene = bpy.context.scene
    try:
        scene.render.engine = "BLENDER_EEVEE_NEXT"
    except TypeError:
        scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.world = bpy.data.worlds.new("hale_world")
    scene.world.color = (0.78, 0.82, 0.86)
    bpy.ops.object.light_add(type="SUN", location=(0, -10, 10))
    sun = bpy.context.object
    sun.name = "sun"
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(45), 0, math.radians(30))
    bpy.ops.object.camera_add()
    scene.camera = bpy.context.object


def import_stl(path: Path) -> bpy.types.Object:
    before = set(bpy.data.objects)
    try:
        bpy.ops.wm.stl_import(filepath=str(path))
    except Exception:
        bpy.ops.import_mesh.stl(filepath=str(path))
    after = [obj for obj in bpy.data.objects if obj not in before]
    if not after:
        raise RuntimeError(f"failed to import {path}")
    obj = after[0]
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    mat = bpy.data.materials.new("mat_carbon_solar")
    mat.diffuse_color = (0.03, 0.05, 0.055, 1.0)
    obj.data.materials.append(mat)
    return obj


def bounds(obj: bpy.types.Object) -> tuple[Vector, Vector, Vector]:
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
    high = Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
    center = (low + high) / 2
    return low, high, center


def look_at(camera: bpy.types.Object, target: Vector) -> None:
    direction = target - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def render_view(obj: bpy.types.Object, out_path: Path, view: str) -> None:
    low, high, center = bounds(obj)
    span = max((high - low).x, (high - low).y, (high - low).z)
    camera = bpy.context.scene.camera
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = span * (1.10 if view != "front" else 0.42)
    if view == "iso":
        camera.location = center + Vector((span * 0.55, -span * 0.95, span * 0.42))
    elif view == "top":
        camera.location = center + Vector((0, 0, span * 1.25))
    elif view == "front":
        camera.location = center + Vector((span * 0.95, 0, span * 0.03))
    else:
        raise ValueError(view)
    look_at(camera, center)
    bpy.context.scene.render.filepath = str(out_path)
    bpy.ops.render.render(write_still=True)


def main() -> int:
    args = parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rows = json.loads(args.selected.read_text(encoding="utf-8"))
    for row in rows:
        reset_scene(args.width, args.height)
        stl = args.models / f"{row['code']}_{row['name'].replace(' ', '_')}.stl"
        obj = import_stl(stl)
        for view in ("iso", "top", "front"):
            render_view(obj, args.out / f"{row['code']}_{view}.png", view)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
