#!/usr/bin/env python3
"""Conceptual HALE redesign focused on endurance and low relative signature."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt
import numpy as np
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Image, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "hale_stealth_endurance"
MODELS = OUT / "models"
OBJS = OUT / "obj"
PLOTS = OUT / "plots"
REPORTS = OUT / "reports"


CANDIDATES = [
    {"code": "SE-01", "name": "Black Ray", "span": 132, "area": 720, "payload": 180, "battery": 2200, "ld": 34.0, "stealth": 0.89, "solar_eff": 0.255, "focus": "balanced stealth endurance"},
    {"code": "SE-02", "name": "Long Eclipse", "span": 190, "area": 1320, "payload": 260, "battery": 4200, "ld": 38.0, "stealth": 0.82, "solar_eff": 0.265, "focus": "maximum endurance"},
    {"code": "SE-03", "name": "Neuron Sail", "span": 142, "area": 820, "payload": 150, "battery": 2700, "ld": 32.0, "stealth": 0.96, "solar_eff": 0.255, "focus": "minimum relative signature"},
    {"code": "SE-04", "name": "Strato Manta", "span": 205, "area": 1480, "payload": 560, "battery": 4700, "ld": 36.0, "stealth": 0.77, "solar_eff": 0.265, "focus": "payload and endurance"},
    {"code": "SE-05", "name": "Quiet Meridian", "span": 164, "area": 1040, "payload": 320, "battery": 3600, "ld": 35.0, "stealth": 0.92, "solar_eff": 0.255, "focus": "stealth payload compromise"},
    {"code": "SE-06", "name": "Night Ledger", "span": 176, "area": 1180, "payload": 230, "battery": 5200, "ld": 37.0, "stealth": 0.85, "solar_eff": 0.265, "focus": "battery reserve"},
]


def analyse(row: dict[str, Any]) -> dict[str, Any]:
    solar_area = row["area"] * 0.82
    solar_mass = solar_area * 0.48
    structure = row["area"] * 1.85 + row["span"] * 2.6
    propulsion = 72 + row["span"] * 0.32
    avionics = 42
    thermal = row["battery"] * 0.04
    gross = structure + solar_mass + row["battery"] + propulsion + avionics + thermal + row["payload"]
    cruise = 21.0 + math.sqrt(max(gross / row["area"], 1.0)) * 1.7
    demand_kw = (gross * 9.80665 * cruise / row["ld"] / 0.84) / 1000.0 + 0.55
    best_solar = solar_area * row["solar_eff"] * 900 * 9.2 / 1000.0
    worst_solar = solar_area * row["solar_eff"] * 430 * 5.6 / 1000.0
    battery_kwh = row["battery"] * 0.52 * 0.86
    best_h = (battery_kwh + best_solar) / demand_kw
    worst_h = (battery_kwh + worst_solar) / (demand_kw * 1.22)
    rcs_rel = (1.0 - row["stealth"]) * 4.0 + (row["span"] / 150.0) * 0.9 + (row["payload"] / 500.0) * 0.35
    score = best_h * 0.55 + worst_h * 0.55 + row["stealth"] * 58.0 + row["payload"] / 55.0 - rcs_rel * 4.0
    return {
        **row,
        "solar_area": solar_area,
        "solar_mass": solar_mass,
        "structure": structure,
        "propulsion": propulsion,
        "gross": gross,
        "cruise": cruise,
        "demand_kw": demand_kw,
        "battery_kwh": battery_kwh,
        "best_solar_kwh_day": best_solar,
        "worst_solar_kwh_day": worst_solar,
        "best_endurance_h": best_h,
        "worst_endurance_h": worst_h,
        "rcs_relative": rcs_rel,
        "score": score,
    }


def wing_vertices(row: dict[str, Any]) -> tuple[list[tuple[float, float, float]], list[tuple[int, int, int]]]:
    span = row["span"]
    area = row["area"]
    root = area / span * 2.25
    mid_chord = root * 0.46
    tip = root * 0.12
    half = span / 2.0
    stations = [
        (0.0, root * 0.50, -root * 0.50, 0.46),
        (half * 0.32, mid_chord * 0.38, -mid_chord * 0.62, 0.23),
        (half * 0.72, tip * 0.25, -tip * 0.95, 0.12),
        (half, -tip * 0.05, -tip * 1.05, 0.055),
    ]
    verts: list[tuple[float, float, float]] = []
    for side in (-1, 1):
        for y, lead, trail, thick in stations:
            yy = side * y
            verts.extend([(lead, yy, thick), (trail, yy, thick * 0.35), (lead, yy, -thick), (trail, yy, -thick * 0.35)])
    faces: list[tuple[int, int, int]] = []
    for side_base in (0, len(stations) * 4):
        for i in range(len(stations) - 1):
            a = side_base + i * 4
            b = side_base + (i + 1) * 4
            faces.extend([(a, b, b + 1), (a, b + 1, a + 1), (a + 2, b + 3, b + 2), (a + 2, a + 3, b + 3)])
            faces.extend([(a, a + 2, b + 2), (a, b + 2, b), (a + 1, b + 1, b + 3), (a + 1, b + 3, a + 3)])
    # center body facets
    c = len(verts)
    length = root * 1.55
    width = span * 0.16
    height = root * 0.18
    verts.extend([(length * 0.45, 0, 0), (0, width / 2, height), (-length * 0.55, 0, height * 0.6), (0, -width / 2, height), (0, width / 2, -height), (-length * 0.50, 0, -height * 0.5), (0, -width / 2, -height)])
    faces.extend([(c, c + 1, c + 2), (c, c + 2, c + 3), (c, c + 4, c + 1), (c, c + 6, c + 4), (c, c + 3, c + 6), (c + 1, c + 4, c + 5), (c + 1, c + 5, c + 2), (c + 3, c + 2, c + 5), (c + 3, c + 5, c + 6)])
    return verts, faces


def write_mesh(row: dict[str, Any]) -> None:
    MODELS.mkdir(parents=True, exist_ok=True)
    OBJS.mkdir(parents=True, exist_ok=True)
    verts, faces = wing_vertices(row)
    stem = f"{row['code']}_{row['name'].replace(' ', '_')}"
    stl = MODELS / f"{stem}.stl"
    with stl.open("w", encoding="utf-8") as handle:
        handle.write(f"solid {stem}\n")
        for a, b, c in faces:
            v1, v2, v3 = np.array(verts[a]), np.array(verts[b]), np.array(verts[c])
            n = np.cross(v2 - v1, v3 - v1)
            n = n / max(np.linalg.norm(n), 1e-9)
            handle.write(f" facet normal {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n  outer loop\n")
            for v in (v1, v2, v3):
                handle.write(f"   vertex {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
            handle.write("  endloop\n endfacet\n")
        handle.write(f"endsolid {stem}\n")
    obj = OBJS / f"{stem}.obj"
    with obj.open("w", encoding="utf-8") as handle:
        handle.write(f"o {stem}\n")
        for v in verts:
            handle.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
        for a, b, c in faces:
            handle.write(f"f {a + 1} {b + 1} {c + 1}\n")


def plot_candidate(row: dict[str, Any]) -> None:
    PLOTS.mkdir(parents=True, exist_ok=True)
    verts, _ = wing_vertices(row)
    xy = np.array([(v[1], v[0]) for v in verts])
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.3), dpi=160)
    hull = np.array([
        (-row["span"] / 2, -row["area"] / row["span"] * 0.12),
        (-row["span"] * 0.36, row["area"] / row["span"] * 0.25),
        (0, row["area"] / row["span"] * 1.1),
        (row["span"] * 0.36, row["area"] / row["span"] * 0.25),
        (row["span"] / 2, -row["area"] / row["span"] * 0.12),
        (0, -row["area"] / row["span"] * 1.0),
    ])
    axes[0].fill(hull[:, 0], hull[:, 1], color="#263238", alpha=0.78)
    axes[0].plot(hull[:, 0], hull[:, 1], color="#111", linewidth=1.0)
    axes[0].set_title(f"{row['code']} planform")
    axes[0].set_aspect("equal", adjustable="box")
    axes[0].grid(True, alpha=0.25)
    axes[0].set_xlabel("span (m)")
    axes[0].set_ylabel("chord axis (m)")

    theta = np.linspace(0, 2 * np.pi, 361)
    best = row["rcs_relative"] * (0.20 + (np.cos(theta) ** 10) * 0.45 + (np.sin(theta) ** 12) * 0.25)
    worst = row["rcs_relative"] * (0.42 + (np.cos(theta) ** 8) * 0.85 + (np.sin(theta) ** 10) * 1.25)
    fig.delaxes(axes[1])
    ax = fig.add_subplot(1, 3, 2, projection="polar")
    ax.plot(theta, best, color="#0b7285", label="best")
    ax.plot(theta, worst, color="#9d0208", label="worst")
    ax.set_title("relative fuselage RCS")
    ax.set_yticklabels([])
    ax.legend(fontsize=7, loc="lower left")

    axes[2].bar(["best", "worst"], [row["best_endurance_h"], row["worst_endurance_h"]], color=["#2a9d8f", "#e9c46a"])
    axes[2].set_title("endurance envelope")
    axes[2].set_ylabel("hours")
    axes[2].grid(axis="y", alpha=0.25)
    fig.suptitle(f"{row['code']} {row['name']} - {row['focus']}", fontweight="bold")
    fig.tight_layout()
    fig.savefig(PLOTS / f"{row['code']}_summary.png")
    plt.close(fig)


def styles():
    st = getSampleStyleSheet()
    st["Title"].fontSize = 17
    st["Heading2"].fontSize = 12
    return st


def table(data: list[list[Any]]) -> Table:
    t = Table([[str(cell) for cell in row] for row in data], repeatRows=1)
    t.setStyle(TableStyle([("GRID", (0, 0), (-1, -1), 0.25, colors.lightgrey), ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#edf2f4")), ("VALIGN", (0, 0), (-1, -1), "TOP")]))
    return t


def component_rows(row: dict[str, Any]) -> list[list[str]]:
    motor_kw = max(row["demand_kw"] * 0.42, 18.0)
    battery_modules = max(2, math.ceil(row["battery"] / 280.0))
    return [
        ["Primary structure", "Carbon sandwich skin, carbon main spar, aluminium service frames"],
        ["High-load fittings", "Titanium inserts on landing gear, motor mounts and detachable wing roots"],
        ["Propulsion", f"4x low-RPM pusher electric motors, {motor_kw:.1f} kW peak each"],
        ["Battery", f"{battery_modules} modular Li-ion packs, {row['battery_kwh']:.0f} kWh usable total"],
        ["Solar", f"{row['solar_area']:.0f} m2 upper-surface high-efficiency array"],
        ["Flight computer", "Jetson Orin Nano class companion + Pixhawk class safety controller"],
        ["Comms", "Dual redundant telemetry: short-range RF + satellite/long-range IP modem bay"],
        ["Thermal", "Motor/controller waste-heat loop routed to battery bay through regulated conduction plate"],
        ["Landing", "Retractable low-profile bogie pods with flush composite doors"],
        ["Control", "Split elevons, drag rudders, inner flaps, four motor throttle channels"],
    ]


def make_individual_report(row: dict[str, Any]) -> None:
    st = styles()
    stem = f"{row['code']}_{row['name'].replace(' ', '_')}"
    doc = SimpleDocTemplate(str(REPORTS / f"{stem}.pdf"), pagesize=A4, rightMargin=14 * mm, leftMargin=14 * mm)
    story: list[Any] = [
        Paragraph(f"{row['code']} {row['name']}", st["Title"]),
        Paragraph(
            "Individual conceptual aircraft sheet. Results are sizing and relative-signature estimates for simulation planning, not certified CFD/RCS data.",
            st["BodyText"],
        ),
        Spacer(1, 4 * mm),
        Image(str(PLOTS / f"{row['code']}_summary.png"), width=175 * mm, height=58 * mm),
        Spacer(1, 4 * mm),
        Paragraph("Sizing", st["Heading2"]),
        table([
            ["Metric", "Value"],
            ["Focus", row["focus"]],
            ["Span", f"{row['span']:.0f} m"],
            ["Wing area", f"{row['area']:.0f} m2"],
            ["Payload", f"{row['payload']:.0f} kg"],
            ["Gross mass", f"{row['gross']:.0f} kg"],
            ["Cruise speed", f"{row['cruise']:.1f} m/s"],
            ["Cruise demand", f"{row['demand_kw']:.1f} kW"],
            ["Best-case endurance", f"{row['best_endurance_h']:.1f} h"],
            ["Worst-case endurance", f"{row['worst_endurance_h']:.1f} h"],
            ["Relative fuselage RCS index", f"{row['rcs_relative']:.3f}"],
        ]),
        Spacer(1, 4 * mm),
        Paragraph("Component Topology", st["Heading2"]),
        table([["Subsystem", "Choice"]] + component_rows(row)),
        Spacer(1, 4 * mm),
        Paragraph("Generated Files", st["Heading2"]),
        table([
            ["Type", "Path"],
            ["STL", f"models/{stem}.stl"],
            ["OBJ", f"obj/{stem}.obj"],
            ["Planform/RCS/endurance plot", f"plots/{row['code']}_summary.png"],
        ]),
    ]
    doc.build(story)


def make_reports(rows: list[dict[str, Any]], selected: list[dict[str, Any]]) -> None:
    REPORTS.mkdir(parents=True, exist_ok=True)
    st = styles()
    doc = SimpleDocTemplate(str(REPORTS / "HALE_STEALTH_ENDURANCE_REDESIGN.pdf"), pagesize=A4, rightMargin=14 * mm, leftMargin=14 * mm)
    story: list[Any] = [
        Paragraph("HALE Stealth-Endurance Redesign", st["Title"]),
        Paragraph("Conceptual blended-wing redesign focused on endurance and low relative signature. This is a comparative concept study, not certified CFD/RCS engineering.", st["BodyText"]),
        Spacer(1, 4 * mm),
        table([["Code", "Focus", "Span", "Payload", "Gross", "Best", "Worst", "RCS rel", "Selected"]] + [[r["code"], r["focus"], f"{r['span']:.0f} m", f"{r['payload']:.0f} kg", f"{r['gross']:.0f} kg", f"{r['best_endurance_h']:.1f} h", f"{r['worst_endurance_h']:.1f} h", f"{r['rcs_relative']:.2f}", "yes" if r in selected else "no"] for r in rows]),
        Spacer(1, 5 * mm),
        Paragraph("Selected Concepts", st["Heading2"]),
    ]
    for row in selected:
        story.append(Paragraph(f"{row['code']} {row['name']}", st["Heading2"]))
        story.append(Image(str(PLOTS / f"{row['code']}_summary.png"), width=175 * mm, height=58 * mm))
        story.append(table([["Metric", "Value"], ["Solar area", f"{row['solar_area']:.1f} m2"], ["Battery usable", f"{row['battery_kwh']:.1f} kWh"], ["Demand", f"{row['demand_kw']:.1f} kW"], ["Material", "carbon spar/skin, titanium inserts, aluminium service frames"], ["Files", f"models/{row['code']}_{row['name'].replace(' ', '_')}.stl and obj/"]]))
        story.append(Spacer(1, 4 * mm))
    doc.build(story)
    for row in rows:
        make_individual_report(row)


def main() -> int:
    rows = [analyse(row) for row in CANDIDATES]
    rows.sort(key=lambda item: item["score"], reverse=True)
    selected = rows[:3]
    OUT.mkdir(parents=True, exist_ok=True)
    for row in rows:
        write_mesh(row)
        plot_candidate(row)
    make_reports(rows, selected)
    (OUT / "candidates.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    (OUT / "selected.json").write_text(json.dumps(selected, indent=2), encoding="utf-8")
    (OUT / "README.md").write_text(
        "# HALE Stealth-Endurance Redesign\n\n"
        "Six blended-wing concepts generated; three selected by endurance, payload and relative signature score.\n"
        "RCS plots are relative conceptual comparisons only, not absolute radar predictions.\n",
        encoding="utf-8",
    )
    print(json.dumps({"out": str(OUT), "candidates": len(rows), "selected": [r["code"] for r in selected]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
