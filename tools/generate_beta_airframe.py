#!/usr/bin/env python3
"""Generate the first Beta airframe CAD proxy, part plan, BOM, and RCS proxy plots."""

from __future__ import annotations

import csv
import json
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import trimesh


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "beta"


def make_body_mesh() -> trimesh.Trimesh:
    # Coordinates are meters. x is nose-to-tail, y is left-right, z is vertical.
    sections = [
        (0.96, 0.00, 0.015, -0.015),
        (0.72, 0.18, 0.070, -0.040),
        (0.45, 0.34, 0.115, -0.065),
        (0.10, 0.62, 0.100, -0.070),
        (-0.34, 0.82, 0.070, -0.060),
        (-0.70, 0.68, 0.050, -0.045),
        (-0.94, 0.32, 0.030, -0.030),
    ]
    vertices: list[tuple[float, float, float]] = []
    for x, half, top, bottom in sections:
        vertices.extend(
            [
                (x, -half, 0.0),
                (x, -half * 0.42, top),
                (x, 0.0, top * 1.12),
                (x, half * 0.42, top),
                (x, half, 0.0),
                (x, half * 0.42, bottom),
                (x, 0.0, bottom * 1.05),
                (x, -half * 0.42, bottom),
            ]
        )
    faces: list[tuple[int, int, int]] = []
    ring = 8
    for i in range(len(sections) - 1):
        a = i * ring
        b = (i + 1) * ring
        for j in range(ring):
            j2 = (j + 1) % ring
            faces.append((a + j, b + j, b + j2))
            faces.append((a + j, b + j2, a + j2))
    faces.extend([(0, 1, 2), (0, 2, 7), (7, 2, 6), (2, 3, 6), (6, 3, 5), (3, 4, 5)])
    last = (len(sections) - 1) * ring
    faces.extend([(last, last + 2, last + 1), (last, last + 7, last + 2), (last + 7, last + 6, last + 2), (last + 2, last + 6, last + 3), (last + 6, last + 5, last + 3), (last + 3, last + 5, last + 4)])
    mesh = trimesh.Trimesh(vertices=np.array(vertices), faces=np.array(faces), process=True)
    mesh.metadata["name"] = "HESIA_Beta_faceted_airframe"
    return mesh


def make_fin(side: float) -> trimesh.Trimesh:
    y0 = side * 0.48
    verts = np.array(
        [
            (-0.48, y0, 0.035),
            (-0.82, y0, 0.030),
            (-0.75, y0 + side * 0.08, 0.330),
            (-0.55, y0 + side * 0.05, 0.250),
            (-0.48, y0 + side * 0.025, 0.020),
            (-0.82, y0 + side * 0.025, 0.020),
            (-0.75, y0 + side * 0.105, 0.310),
            (-0.55, y0 + side * 0.075, 0.235),
        ]
    )
    faces = np.array(
        [
            (0, 1, 2),
            (0, 2, 3),
            (4, 6, 5),
            (4, 7, 6),
            (0, 4, 5),
            (0, 5, 1),
            (1, 5, 6),
            (1, 6, 2),
            (2, 6, 7),
            (2, 7, 3),
            (3, 7, 4),
            (3, 4, 0),
        ]
    )
    mesh = trimesh.Trimesh(vertices=verts, faces=faces, process=True)
    mesh.metadata["name"] = "left_fin" if side < 0 else "right_fin"
    return mesh


def make_nozzle() -> trimesh.Trimesh:
    radius = 0.085
    length = 0.17
    sections = 32
    x0 = -0.965 - length / 2
    x1 = -0.965 + length / 2
    vertices = []
    for x in (x0, x1):
        for i in range(sections):
            theta = 2 * math.pi * i / sections
            vertices.append((x, radius * math.cos(theta), -0.005 + radius * math.sin(theta)))
    faces = []
    for i in range(sections):
        j = (i + 1) % sections
        faces.append((i, sections + i, sections + j))
        faces.append((i, sections + j, j))
    center0 = len(vertices)
    center1 = center0 + 1
    vertices.extend([(x0, 0.0, -0.005), (x1, 0.0, -0.005)])
    for i in range(sections):
        j = (i + 1) % sections
        faces.append((center0, j, i))
        faces.append((center1, sections + i, sections + j))
    cyl = trimesh.Trimesh(vertices=np.array(vertices), faces=np.array(faces), process=True)
    cyl.metadata["name"] = "edf_nozzle_90mm_proxy"
    return cyl


def export_meshes(mesh: trimesh.Trimesh) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    mesh.export(OUT / "HESIA_Beta_airframe.obj")
    mesh.export(OUT / "HESIA_Beta_airframe.stl")


def facet_rcs_proxy(mesh: trimesh.Trimesh, azimuth_deg: np.ndarray, elevation_deg: float = 0.0) -> np.ndarray:
    normals = mesh.face_normals
    areas = mesh.area_faces
    elev = math.radians(elevation_deg)
    scores = []
    for az in np.deg2rad(azimuth_deg):
        incoming = np.array([math.cos(elev) * math.cos(az), math.cos(elev) * math.sin(az), math.sin(elev)])
        cos_term = np.maximum(0.0, normals @ incoming)
        # Geometric monostatic proxy only, not an EM solver.
        scores.append(float(np.sum(areas * cos_term**4)))
    return np.array(scores)


def write_rcs(mesh: trimesh.Trimesh) -> None:
    az = np.linspace(0.0, 360.0, 361)
    broadside = facet_rcs_proxy(mesh, az, elevation_deg=0.0)
    elevated = facet_rcs_proxy(mesh, az, elevation_deg=10.0)
    db_broad = 10 * np.log10(np.maximum(broadside, 1e-8))
    db_elev = 10 * np.log10(np.maximum(elevated, 1e-8))

    with (OUT / "HESIA_Beta_RCS_proxy.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["azimuth_deg", "proxy_dbsm_elev0", "proxy_dbsm_elev10"])
        for a, b, c in zip(az, db_broad, db_elev):
            writer.writerow([f"{a:.1f}", f"{b:.4f}", f"{c:.4f}"])

    plt.figure(figsize=(9, 4.8))
    plt.plot(az, db_broad, label="elevation 0 deg")
    plt.plot(az, db_elev, label="elevation 10 deg")
    plt.xlabel("Azimuth aspect (deg)")
    plt.ylabel("Geometric RCS proxy (dBsm)")
    plt.title("HESIA Beta faceted airframe proxy, not EM RCS")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(OUT / "HESIA_Beta_RCS_proxy.png", dpi=180)
    plt.close()

    summary = {
        "warning": "Geometric facet proxy only. This is not an electromagnetic RCS simulation.",
        "best_elev0_dbsm": float(db_broad.min()),
        "best_elev0_azimuth_deg": float(az[int(db_broad.argmin())]),
        "worst_elev0_dbsm": float(db_broad.max()),
        "worst_elev0_azimuth_deg": float(az[int(db_broad.argmax())]),
        "best_elev10_dbsm": float(db_elev.min()),
        "best_elev10_azimuth_deg": float(az[int(db_elev.argmin())]),
        "worst_elev10_dbsm": float(db_elev.max()),
        "worst_elev10_azimuth_deg": float(az[int(db_elev.argmax())]),
    }
    (OUT / "HESIA_Beta_RCS_proxy_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")


def write_parts_and_bom() -> None:
    parts = {
        "scale": "1.90 m length, 1.64 m wingspan proxy, units in meters",
        "parts": [
            {"name": "center fuselage shell", "material": "LW-PLA or PETG-CF", "notes": "Split into nose, avionics bay, EDF bay, aft nozzle."},
            {"name": "left wing shell", "material": "LW-PLA ribs with carbon tube spar", "notes": "Includes elevon hinge line and servo pocket."},
            {"name": "right wing shell", "material": "LW-PLA ribs with carbon tube spar", "notes": "Mirror of left wing."},
            {"name": "EDF duct and nozzle", "material": "PETG-CF or nylon-CF", "notes": "90 mm EDF proxy, removable for service."},
            {"name": "main landing gear bay", "material": "nylon-CF mount plates", "notes": "Retract units fixed to plywood or carbon plates."},
            {"name": "nose landing gear bay", "material": "nylon-CF mount plate", "notes": "Steerable retract, isolated from Jetson bay."},
            {"name": "avionics tray", "material": "carbon plate", "notes": "Jetson Orin Nano, Pixhawk, RF chamber, power regulators."},
        ],
        "control_surfaces": [
            {"name": "left elevon", "actuation": "metal gear mini servo, internal linkage"},
            {"name": "right elevon", "actuation": "metal gear mini servo, internal linkage"},
            {"name": "split flap option", "actuation": "two slim servos, optional, must be flight-tested separately"},
            {"name": "landing gear", "actuation": "electric retracts, nose steering servo"},
        ],
    }
    (OUT / "HESIA_Beta_parts_plan.json").write_text(json.dumps(parts, indent=2), encoding="utf-8")

    rows = [
        ["Category", "Selected component", "Qty", "Rationale"],
        ["Propulsion", "90 mm 12-blade EDF, 8S capable, 1700-1900 kV", "1", "Matches 1.6-1.9 m class prototype while leaving payload margin."],
        ["ESC", "120 A HV ESC with telemetry", "1", "Headroom for EDF current spikes."],
        ["Battery", "8S 5000 mAh LiPo, 60C minimum", "1", "Practical compromise between thrust, mass, and flight time."],
        ["Flight computer", "Pixhawk 6C or equivalent", "1", "Primary stabilized flight and failsafe controller."],
        ["Companion computer", "Jetson Orin Nano 8 GB", "1", "Runs perception and mission AI."],
        ["RF chamber", "Shielded avionics/RF bay with filtered pass-throughs", "1", "Physical separation for radio, GNSS, and compute noise."],
        ["Servos", "Metal gear slim digital servos, 5-8 kg.cm", "4", "Elevons plus optional flaps."],
        ["Landing gear", "Electric retract tricycle set for 3-5 kg EDF jets", "1", "Nose steering plus two mains."],
        ["Structure", "Carbon tube spar 12-16 mm plus flat carbon strips", "1 set", "Wing stiffness and removable panels."],
        ["Material", "LW-PLA, PETG-CF, nylon-CF, plywood/carbon plates", "1 set", "Printable shells and stronger mounts."],
        ["Power", "5 V/6 A and 12 V regulators with LC filtering", "1 set", "Separate avionics and compute power rails."],
        ["Telemetry", "ELRS or equivalent RC link plus MAVLink telemetry", "1 set", "Pilot control and monitoring."],
        ["Sensors", "GNSS, airspeed sensor, IMU already in Pixhawk, optional optical flow", "1 set", "Flight stabilization and logging."],
    ]
    with (OUT / "HESIA_Beta_components.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerows(rows)


def write_report(mesh: trimesh.Trimesh) -> None:
    summary = json.loads((OUT / "HESIA_Beta_RCS_proxy_summary.json").read_text(encoding="utf-8"))
    report = f"""# HESIA Beta Airframe Package

Generated: 2026-05-09

## Scope

This package is a first constructible CAD proxy inspired by the supplied visual reference video. It is not a validated aerodynamic design and not an electromagnetic stealth simulation. The RCS graph is a geometric facet proxy generated from the mesh normals and projected areas.

## Video Evidence

- Source: `Modelisation/Visuel.mp4`
- Metadata extracted: 1280x720, 30 fps, 653 frames, 21.77 seconds.
- Contact sheet: `artifacts/beta/Visuel_contact_sheet.jpg`

## Geometry Outputs

- `HESIA_Beta_airframe.obj`
- `HESIA_Beta_airframe.stl`
- Mesh surface area: {mesh.area:.3f} m2
- Mesh bounding box: {mesh.extents[0]:.3f} m length x {mesh.extents[1]:.3f} m span x {mesh.extents[2]:.3f} m height

## Topology

Chosen topology:

- Faceted blended wing body, approximately 1.90 m long and 1.64 m span.
- Single aft 90 mm EDF proxy.
- Battery ahead of center of gravity, Jetson/Pixhawk/RF bay near center, EDF aft.
- Tricycle retract landing gear: steerable nose gear, two main retracts in wing roots.
- Elevons as primary controls, optional split flaps only after separate flight testing.

## Component List

The shopping list is in `HESIA_Beta_components.csv`.

Main selections:

- Battery: 8S 5000 mAh LiPo, 60C minimum.
- EDF: 90 mm 12-blade 8S-capable unit.
- ESC: 120 A HV telemetry ESC.
- Flight stack: Pixhawk 6C plus Jetson Orin Nano 8 GB.
- Structure: LW-PLA shells, PETG-CF/nylon-CF ducts and mounts, carbon spar.

## RCS Proxy Result

Warning: this is not real RCS. It is a repeatable geometric proxy suitable only for comparing mesh revisions.

- Best 0 deg elevation proxy: {summary["best_elev0_dbsm"]:.2f} dBsm at azimuth {summary["best_elev0_azimuth_deg"]:.0f} deg.
- Worst 0 deg elevation proxy: {summary["worst_elev0_dbsm"]:.2f} dBsm at azimuth {summary["worst_elev0_azimuth_deg"]:.0f} deg.
- Best 10 deg elevation proxy: {summary["best_elev10_dbsm"]:.2f} dBsm at azimuth {summary["best_elev10_azimuth_deg"]:.0f} deg.
- Worst 10 deg elevation proxy: {summary["worst_elev10_dbsm"]:.2f} dBsm at azimuth {summary["worst_elev10_azimuth_deg"]:.0f} deg.

Graph and data:

- `HESIA_Beta_RCS_proxy.png`
- `HESIA_Beta_RCS_proxy.csv`
- `HESIA_Beta_RCS_proxy_summary.json`

## Construction Notes

- Print the fuselage in serviceable modules: nose, avionics bay, battery bay, EDF bay, aft nozzle.
- Use carbon spar tubes across the wing roots, not printed plastic alone.
- Keep the Jetson bay thermally ventilated but RF-separated from receiver/GNSS wiring.
- Do not rely on the proxy mesh for center-of-gravity or structural validation. A real build requires mass properties, thrust tests, CG balancing, and low-speed glide testing.

"""
    (OUT / "HESIA_Beta_airframe_report.md").write_text(report, encoding="utf-8")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    mesh = trimesh.util.concatenate([make_body_mesh(), make_fin(-1.0), make_fin(1.0), make_nozzle()])
    export_meshes(mesh)
    write_rcs(mesh)
    write_parts_and_bom()
    write_report(mesh)
    print(f"Generated Beta package in {OUT}")


if __name__ == "__main__":
    main()
