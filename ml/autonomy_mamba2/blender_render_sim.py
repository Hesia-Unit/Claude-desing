"""Blender-side procedural visual simulator for autonomy dataset frames.

Run with Blender, for example:

blender.exe -b --python ml/autonomy_mamba2/blender_render_sim.py -- --manifest scenario_manifest.json --out-dir frames --frames 120
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
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=160)
    parser.add_argument("--width", type=int, default=384)
    parser.add_argument("--height", type=int, default=256)
    parser.add_argument("--metadata", type=Path)
    return parser.parse_args(argv)


def material(name: str, color: tuple[float, float, float, float]) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Roughness"].default_value = 0.82
    return mat


def add_cube(name: str, location: tuple[float, float, float], scale: tuple[float, float, float], mat: bpy.types.Material) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    obj.data.materials.append(mat)
    return obj


def add_cylinder(name: str, location: tuple[float, float, float], radius: float, depth: float, mat: bpy.types.Material) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=radius, depth=depth, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def add_cone(name: str, location: tuple[float, float, float], radius: float, depth: float, mat: bpy.types.Material) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cone_add(vertices=24, radius1=radius, depth=depth, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def look_at(obj: bpy.types.Object, target: Vector) -> None:
    direction = target - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def reset_scene(width: int, height: int) -> dict[str, bpy.types.Material]:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    scene = bpy.context.scene
    try:
        scene.render.engine = "BLENDER_EEVEE_NEXT"
    except TypeError:
        scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Filmic"
    scene.view_settings.look = "Medium High Contrast"
    scene.world = bpy.data.worlds.new("HESIA Sky")
    scene.world.color = (0.52, 0.70, 0.95)

    mats = {
        "ground": material("ground_mixed_grass", (0.21, 0.35, 0.18, 1.0)),
        "road": material("road_dark_asphalt", (0.12, 0.12, 0.12, 1.0)),
        "building": material("building_concrete", (0.58, 0.55, 0.50, 1.0)),
        "trunk": material("tree_trunk", (0.34, 0.20, 0.11, 1.0)),
        "leaf": material("tree_leaf", (0.05, 0.33, 0.12, 1.0)),
        "obstacle": material("red_obstacle", (0.75, 0.08, 0.05, 1.0)),
        "target": material("target_marker", (0.95, 0.75, 0.05, 1.0)),
    }

    add_cube("ground_plane", (0, 0, -0.05), (80, 80, 0.05), mats["ground"])
    add_cube("road_corridor", (0, 4, 0.01), (9, 80, 0.02), mats["road"])
    for x in (-18, -10, 13, 22):
        for y in (-18, 2, 22):
            height_obj = 3.0 + ((abs(x) + abs(y)) % 5)
            add_cube("building", (x, y, height_obj / 2), (2.8, 3.5, height_obj), mats["building"])
    for x in (-28, -23, -16, 16, 23, 29):
        for y in (-25, -9, 8, 26):
            add_cylinder("tree_trunk", (x, y, 0.9), 0.22, 1.8, mats["trunk"])
            add_cone("tree_leaf", (x, y, 2.45), 1.35, 2.6, mats["leaf"])
    for idx, x in enumerate((-5, 6, 0, 10)):
        add_cube("obstacle_box", (x, -4 + idx * 11, 0.7), (0.9, 0.9, 0.7), mats["obstacle"])
    add_cone("mission_target", (0, 34, 1.6), 1.2, 3.2, mats["target"])

    bpy.ops.object.light_add(type="SUN", location=(0, 0, 12))
    sun = bpy.context.object
    sun.name = "sun_key"
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(42), 0, math.radians(25))

    bpy.ops.object.camera_add(location=(0, -28, 11), rotation=(math.radians(68), 0, 0))
    camera = bpy.context.object
    scene.camera = camera
    camera.data.lens = 24
    camera.data.dof.use_dof = True
    camera.data.dof.aperture_fstop = 8.0
    return mats


def semantic_from_scenario(scenario: dict[str, object], idx: int, total: int) -> dict[str, float]:
    disturbance = scenario.get("disturbance") if isinstance(scenario.get("disturbance"), dict) else {}
    progress = idx / max(total - 1, 1)
    base_obstacle = 0.18 + 0.08 * math.sin(idx * 0.17)
    if disturbance.get("stall"):
        base_obstacle += 0.08
    if disturbance.get("motor_loss"):
        base_obstacle += 0.04
    safe = max(0.05, 0.78 - base_obstacle)
    return {
        "mask_coverage": {
            "road": safe,
            "sky": 0.24 + 0.04 * math.cos(idx * 0.1),
            "tree": 0.11,
            "building": 0.10,
            "truck": 0.02 if idx % 9 == 0 else 0.0,
        },
        "obstacle_fraction": min(0.95, base_obstacle),
        "safe_surface_fraction": safe,
        "center_obstacle_fraction": 0.09 + 0.04 * math.sin(idx * 0.31),
        "left_obstacle_fraction": 0.12 + 0.04 * math.sin(idx * 0.21),
        "right_obstacle_fraction": 0.11 + 0.04 * math.cos(idx * 0.23),
        "horizon_clearance": max(0.2, 0.85 - (0.18 if disturbance.get("stall") else 0.0)),
        "motion_blur_score": 0.18 if disturbance.get("gust") else 0.05,
        "exposure_risk": 0.15 if disturbance.get("gps_noise") else 0.04,
        "step_progress": progress,
    }


def render_frames(args: argparse.Namespace) -> None:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    scenarios = manifest.get("scenarios") or []
    if not scenarios:
        raise SystemExit("scenario manifest has no scenarios")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    metadata_path = args.metadata or args.out_dir.parent / "blender_render_metadata.jsonl"
    reset_scene(args.width, args.height)
    camera = bpy.context.scene.camera

    with metadata_path.open("w", encoding="utf-8") as fh:
        for frame_idx in range(args.frames):
            scenario = scenarios[frame_idx % len(scenarios)]
            disturbance = scenario.get("disturbance") if isinstance(scenario.get("disturbance"), dict) else {}
            phase = frame_idx / max(args.frames - 1, 1)
            lateral = math.sin(frame_idx * 0.17) * (4.0 if disturbance.get("crosswind") else 1.6)
            altitude = 9.5
            if disturbance.get("stall"):
                altitude -= 2.0 * math.sin(min(math.pi, phase * math.pi))
            if disturbance.get("motor_loss"):
                lateral += 2.4
            camera.location = Vector((lateral, -31 + phase * 32, altitude))
            look_at(camera, Vector((0, 9 + phase * 22, 1.8)))
            if disturbance.get("gust"):
                camera.rotation_euler.z += math.radians(math.sin(frame_idx * 1.3) * 2.5)

            output = args.out_dir / f"frame_{frame_idx:06d}.png"
            bpy.context.scene.render.filepath = str(output)
            bpy.ops.render.render(write_still=True)
            semantic = semantic_from_scenario(scenario, frame_idx, args.frames)
            fh.write(
                json.dumps(
                    {
                        "frame_index": frame_idx,
                        "frame_path": str(output),
                        "scenario_id": scenario.get("scenario_id"),
                        "disturbance": disturbance,
                        "semantic": semantic,
                    },
                    sort_keys=True,
                )
                + "\n"
            )


def main() -> int:
    render_frames(parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
