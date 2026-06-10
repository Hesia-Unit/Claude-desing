#!/usr/bin/env python3
"""Preliminary HALE solar glider sizing, ranking, STL and report generation.

This is a conceptual engineering tool, not a certified CFD/RCS solver. It uses
first-order atmosphere, lift/drag, energy balance, thermal and relative
signature models so the candidate architectures can be compared consistently.
"""

from __future__ import annotations

import csv
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "hale_solar_glider"
MODELS = OUT / "models"
RENDERS = OUT / "renders"
REPORTS = OUT / "reports"

G = 9.80665


@dataclass(slots=True)
class Candidate:
    version: str
    code: str
    name: str
    focus: str
    material: str
    altitude_m: float
    span_m: float
    aspect_ratio: float
    payload_kg: float
    battery_kg: float
    avionics_kg: float
    propulsion_kg: float
    fixed_equipment_kg: float
    solar_coverage: float
    cell_efficiency: float
    battery_whkg: float
    cd0: float
    oswald: float
    prop_efficiency: float
    motor_efficiency: float
    stealth_alignment: float
    vertical_tail_factor: float
    prop_signature_factor: float
    material_factor: float
    structure_coeff: float
    solar_kg_m2: float
    motor_choice: str
    battery_choice: str
    comms_choice: str


def isa_density(alt_m: float) -> float:
    """ISA density up to the lower stratosphere."""
    if alt_m <= 11000:
        t0 = 288.15
        lapse = -0.0065
        t = t0 + lapse * alt_m
        p = 101325.0 * (t / t0) ** (-G / (lapse * 287.05287))
    else:
        t11 = 216.65
        p11 = 22632.06
        p = p11 * math.exp(-G * (alt_m - 11000.0) / (287.05287 * t11))
        t = t11
    return p / (287.05287 * t)


def material_allowable_proxy(version: str) -> float:
    return {"V1": 1.00, "V2": 1.35, "V3": 2.25}[version]


def simulate_energy(
    battery_kwh: float,
    demand_kw: float,
    solar_kwh_day: float,
    daylight_h: float,
    max_hours: int = 240,
) -> float:
    """Hourly battery simulation starting at full charge."""
    if demand_kw <= 0:
        return float(max_hours)
    charge = battery_kwh
    for hour in range(max_hours):
        local = hour % 24
        daylight_start = 12 - daylight_h / 2
        daylight_end = 12 + daylight_h / 2
        solar_kw = 0.0
        if daylight_start <= local < daylight_end:
            phase = (local - daylight_start) / daylight_h
            norm = math.sin(math.pi * phase)
            solar_kw = solar_kwh_day * norm / (daylight_h * 2 / math.pi)
        charge += solar_kw - demand_kw
        charge = min(charge, battery_kwh)
        if charge <= 0:
            return hour + max(0.0, charge + demand_kw) / demand_kw
    return float(max_hours)


def compute(c: Candidate) -> dict[str, Any]:
    s = c.span_m**2 / c.aspect_ratio
    solar_area = s * c.solar_coverage
    root_chord = 1.35 * s / c.span_m
    tip_chord = 0.65 * s / c.span_m
    fuselage_length = max(0.34 * c.span_m, 10.0)
    fuselage_width = max(0.045 * c.span_m, 1.1)
    fuselage_height = max(0.035 * c.span_m, 0.85)
    structure_kg = c.structure_coeff * (0.55 * s + 0.012 * c.span_m**2.1) + 0.055 * c.payload_kg + 0.035 * c.battery_kg
    solar_kg = solar_area * c.solar_kg_m2
    wiring_thermal_kg = 0.055 * c.battery_kg + 0.018 * c.span_m + 0.015 * c.payload_kg
    gross_kg = (
        structure_kg
        + solar_kg
        + c.payload_kg
        + c.battery_kg
        + c.avionics_kg
        + c.propulsion_kg
        + c.fixed_equipment_kg
        + wiring_thermal_kg
    )
    rho = isa_density(c.altitude_m)
    w = gross_kg * G
    cl_target = 0.72
    v = math.sqrt(max(1e-9, 2.0 * w / (rho * s * cl_target)))
    q = 0.5 * rho * v * v
    cl = w / (q * s)
    k = 1.0 / (math.pi * c.oswald * c.aspect_ratio)
    cd = c.cd0 + k * cl * cl
    ld = cl / cd
    drag_n = w / ld
    propulsive_kw = drag_n * v / (1000.0 * c.prop_efficiency * c.motor_efficiency)
    avionics_kw = (28.0 + 0.35 * c.payload_kg + (7.0 if "Iridium" in c.comms_choice else 0.0)) / 1000.0
    motor_waste_w = propulsive_kw * 1000.0 * max(0.0, 1.0 - c.motor_efficiency)
    conducted_heat_w = 0.28 * motor_waste_w
    battery_heat_need_w = 0.72 * c.battery_kg
    heater_extra_w = max(0.0, battery_heat_need_w - conducted_heat_w)
    demand_best_kw = propulsive_kw + avionics_kw
    demand_worst_kw = propulsive_kw * 1.18 + avionics_kw + heater_extra_w / 1000.0

    install_eff = 0.88
    best_daylight_h = 13.2
    worst_daylight_h = 9.2
    best_solar_kwh = solar_area * 1060.0 * c.cell_efficiency * install_eff * 0.63 * best_daylight_h / 1000.0
    worst_solar_kwh = solar_area * 930.0 * c.cell_efficiency * install_eff * 0.36 * worst_daylight_h / 1000.0
    battery_usable_kwh = c.battery_kg * c.battery_whkg * 0.84 / 1000.0
    best_hours = simulate_energy(battery_usable_kwh, demand_best_kw, best_solar_kwh, best_daylight_h)
    worst_hours = simulate_energy(battery_usable_kwh, demand_worst_kw, worst_solar_kwh, worst_daylight_h)
    night_best_need = demand_best_kw * (24.0 - best_daylight_h)
    night_worst_need = demand_worst_kw * (24.0 - worst_daylight_h)
    solar_margin_best = best_solar_kwh / max(1e-6, demand_best_kw * 24.0)
    solar_margin_worst = worst_solar_kwh / max(1e-6, demand_worst_kw * 24.0)

    bending_proxy = w * c.span_m / 8.0 / 1000.0
    margin = material_allowable_proxy(c.version) * (0.012 * c.span_m**2.0 + 0.10 * s) / max(1e-6, bending_proxy / 1000.0)
    frontal_area = fuselage_width * fuselage_height + 0.008 * s
    rcs_relative = frontal_area * c.stealth_alignment * c.material_factor * c.prop_signature_factor * c.vertical_tail_factor
    rcs_score = 100.0 / (1.0 + rcs_relative)
    wing_loading = gross_kg / s
    payload_fraction = c.payload_kg / gross_kg
    score = (
        min(worst_hours, 96.0) / 96.0 * 0.30
        + min(best_hours, 168.0) / 168.0 * 0.18
        + min(payload_fraction / 0.28, 1.0) * 0.17
        + min(ld / 38.0, 1.0) * 0.13
        + min(rcs_score / 35.0, 1.0) * 0.12
        + min(margin / 2.2, 1.0) * 0.06
        + min(solar_margin_worst / 0.82, 1.0) * 0.04
    )

    return {
        **asdict(c),
        "wing_area_m2": s,
        "solar_area_m2": solar_area,
        "root_chord_m": root_chord,
        "tip_chord_m": tip_chord,
        "fuselage_length_m": fuselage_length,
        "fuselage_width_m": fuselage_width,
        "fuselage_height_m": fuselage_height,
        "structure_kg": structure_kg,
        "solar_array_kg": solar_kg,
        "wiring_thermal_kg": wiring_thermal_kg,
        "gross_mass_kg": gross_kg,
        "density_kg_m3": rho,
        "cruise_speed_mps": v,
        "cl": cl,
        "cd": cd,
        "ld": ld,
        "drag_n": drag_n,
        "propulsive_kw": propulsive_kw,
        "avionics_kw": avionics_kw,
        "demand_best_kw": demand_best_kw,
        "demand_worst_kw": demand_worst_kw,
        "motor_waste_w": motor_waste_w,
        "conducted_heat_w": conducted_heat_w,
        "battery_heat_need_w": battery_heat_need_w,
        "heater_extra_w": heater_extra_w,
        "battery_usable_kwh": battery_usable_kwh,
        "solar_best_kwh_day": best_solar_kwh,
        "solar_worst_kwh_day": worst_solar_kwh,
        "best_endurance_h": best_hours,
        "worst_endurance_h": worst_hours,
        "best_endurance_label": ">=10 days" if best_hours >= 240 else f"{best_hours:.1f} h",
        "worst_endurance_label": ">=10 days" if worst_hours >= 240 else f"{worst_hours:.1f} h",
        "night_best_need_kwh": night_best_need,
        "night_worst_need_kwh": night_worst_need,
        "solar_margin_best": solar_margin_best,
        "solar_margin_worst": solar_margin_worst,
        "wing_loading_kg_m2": wing_loading,
        "payload_fraction": payload_fraction,
        "bending_proxy_knm": bending_proxy,
        "structural_margin_proxy": margin,
        "rcs_relative": rcs_relative,
        "rcs_score": rcs_score,
        "balanced_score": score,
    }


def candidates() -> list[Candidate]:
    common_v1 = {
        "version": "V1",
        "material": "carbone + aluminium 7075 + inserts fibre de verre",
        "altitude_m": 18000.0,
        "cell_efficiency": 0.235,
        "battery_whkg": 430.0,
        "oswald": 0.82,
        "prop_efficiency": 0.78,
        "motor_efficiency": 0.92,
        "material_factor": 0.82,
        "structure_coeff": 1.12,
        "solar_kg_m2": 0.74,
        "motor_choice": "4x T-MOTOR U15II / classe grand propulseur BLDC, helices lentes repliees",
        "battery_choice": "Amprius SiMaxx/SiCore pack 430 Wh/kg nominal, 84% usable",
        "comms_choice": "Skytrac DLS-140 Iridium Certus 100 + LTE/LoRa fallback",
    }
    common_v2 = {
        "version": "V2",
        "material": "carbone haut module + aluminium + longerons hybrides verre/carbone",
        "altitude_m": 21000.0,
        "cell_efficiency": 0.245,
        "battery_whkg": 450.0,
        "oswald": 0.84,
        "prop_efficiency": 0.80,
        "motor_efficiency": 0.94,
        "material_factor": 0.74,
        "structure_coeff": 0.96,
        "solar_kg_m2": 0.66,
        "motor_choice": "2x EMRAX 188 derates + grands propulseurs pliants, architecture pusher",
        "battery_choice": "Amprius SiMaxx 450 Wh/kg pack modulaire stratospherique",
        "comms_choice": "Skytrac DLS-140 Iridium Certus 100 + liaison LOS directionnelle",
    }
    common_v3 = {
        "version": "V3",
        "material": "titane + carbone haut module + noeuds de charge composites",
        "altitude_m": 23500.0,
        "cell_efficiency": 0.315,
        "battery_whkg": 500.0,
        "oswald": 0.86,
        "prop_efficiency": 0.82,
        "motor_efficiency": 0.95,
        "material_factor": 0.63,
        "structure_coeff": 0.78,
        "solar_kg_m2": 0.58,
        "motor_choice": "4x EMRAX 188 derates ou axial-flux equivalent, pusher encastre",
        "battery_choice": "Amprius 500 Wh/kg platform / silicon anode pack, 84% usable",
        "comms_choice": "Iridium Certus 100 + double liaison LOS + mode store-and-forward",
    }
    rows = [
        Candidate(code="V1-A", name="Dawn Moth", focus="polyvalence V1", span_m=58, aspect_ratio=31, payload_kg=55, battery_kg=260, avionics_kg=10, propulsion_kg=22, fixed_equipment_kg=18, solar_coverage=0.84, cd0=0.020, stealth_alignment=0.60, vertical_tail_factor=1.02, prop_signature_factor=0.90, **common_v1),
        Candidate(code="V1-B", name="Cargo Heron", focus="charge utile V1", span_m=62, aspect_ratio=28, payload_kg=110, battery_kg=300, avionics_kg=12, propulsion_kg=26, fixed_equipment_kg=24, solar_coverage=0.82, cd0=0.023, stealth_alignment=0.66, vertical_tail_factor=1.08, prop_signature_factor=0.93, **common_v1),
        Candidate(code="V1-C", name="Low Observable Ray", focus="signature faible", span_m=60, aspect_ratio=33, payload_kg=48, battery_kg=285, avionics_kg=11, propulsion_kg=23, fixed_equipment_kg=20, solar_coverage=0.86, cd0=0.019, stealth_alignment=0.40, vertical_tail_factor=0.82, prop_signature_factor=0.82, **common_v1),
        Candidate(code="V1-D", name="Endurance Needle", focus="endurance", span_m=68, aspect_ratio=36, payload_kg=38, battery_kg=380, avionics_kg=10, propulsion_kg=28, fixed_equipment_kg=22, solar_coverage=0.88, cd0=0.018, stealth_alignment=0.56, vertical_tail_factor=0.94, prop_signature_factor=0.86, **common_v1),
        Candidate(code="V1-E", name="Compact Courier", focus="taille reduite", span_m=42, aspect_ratio=25, payload_kg=24, battery_kg=140, avionics_kg=8, propulsion_kg=16, fixed_equipment_kg=12, solar_coverage=0.78, cd0=0.023, stealth_alignment=0.68, vertical_tail_factor=1.12, prop_signature_factor=0.94, **common_v1),
        Candidate(code="V1-F", name="Thermal Hawk", focus="gestion thermique batteries", span_m=56, aspect_ratio=29, payload_kg=70, battery_kg=290, avionics_kg=11, propulsion_kg=28, fixed_equipment_kg=32, solar_coverage=0.83, cd0=0.022, stealth_alignment=0.62, vertical_tail_factor=1.00, prop_signature_factor=0.84, **common_v1),
        Candidate(code="V2-A", name="Strato Albatross", focus="polyvalence V2", span_m=70, aspect_ratio=28, payload_kg=140, battery_kg=340, avionics_kg=16, propulsion_kg=35, fixed_equipment_kg=32, solar_coverage=0.82, cd0=0.020, stealth_alignment=0.52, vertical_tail_factor=0.95, prop_signature_factor=0.84, **common_v2),
        Candidate(code="V2-B", name="Payload Condor", focus="charge utile V2", span_m=76, aspect_ratio=25, payload_kg=260, battery_kg=390, avionics_kg=18, propulsion_kg=42, fixed_equipment_kg=40, solar_coverage=0.80, cd0=0.023, stealth_alignment=0.58, vertical_tail_factor=1.02, prop_signature_factor=0.86, **common_v2),
        Candidate(code="V2-C", name="Silent Manta", focus="signature faible V2", span_m=74, aspect_ratio=30, payload_kg=130, battery_kg=360, avionics_kg=16, propulsion_kg=36, fixed_equipment_kg=34, solar_coverage=0.84, cd0=0.019, stealth_alignment=0.35, vertical_tail_factor=0.78, prop_signature_factor=0.76, **common_v2),
        Candidate(code="V2-D", name="Long Night", focus="reserve batterie", span_m=82, aspect_ratio=31, payload_kg=110, battery_kg=470, avionics_kg=16, propulsion_kg=38, fixed_equipment_kg=36, solar_coverage=0.84, cd0=0.019, stealth_alignment=0.50, vertical_tail_factor=0.94, prop_signature_factor=0.82, **common_v2),
        Candidate(code="V2-E", name="Comms Plateau", focus="charge telecom", span_m=66, aspect_ratio=24, payload_kg=180, battery_kg=330, avionics_kg=22, propulsion_kg=36, fixed_equipment_kg=44, solar_coverage=0.78, cd0=0.024, stealth_alignment=0.60, vertical_tail_factor=1.06, prop_signature_factor=0.88, **common_v2),
        Candidate(code="V2-F", name="High Lift Bridge", focus="portance et marge structure", span_m=88, aspect_ratio=27, payload_kg=210, battery_kg=430, avionics_kg=18, propulsion_kg=44, fixed_equipment_kg=42, solar_coverage=0.83, cd0=0.021, stealth_alignment=0.54, vertical_tail_factor=0.98, prop_signature_factor=0.84, **common_v2),
        Candidate(code="V3-A", name="Titan Stratos", focus="polyvalence V3", span_m=120, aspect_ratio=32, payload_kg=620, battery_kg=1100, avionics_kg=32, propulsion_kg=88, fixed_equipment_kg=95, solar_coverage=0.86, cd0=0.018, stealth_alignment=0.38, vertical_tail_factor=0.76, prop_signature_factor=0.70, **common_v3),
        Candidate(code="V3-B", name="Sky Freighter", focus="charge utile maximale", span_m=145, aspect_ratio=29, payload_kg=1250, battery_kg=1450, avionics_kg=40, propulsion_kg=120, fixed_equipment_kg=140, solar_coverage=0.84, cd0=0.021, stealth_alignment=0.48, vertical_tail_factor=0.86, prop_signature_factor=0.76, **common_v3),
        Candidate(code="V3-C", name="Black Sail", focus="signature minimale", span_m=128, aspect_ratio=35, payload_kg=520, battery_kg=1250, avionics_kg=34, propulsion_kg=96, fixed_equipment_kg=105, solar_coverage=0.88, cd0=0.017, stealth_alignment=0.24, vertical_tail_factor=0.62, prop_signature_factor=0.62, **common_v3),
        Candidate(code="V3-D", name="Aurora Shelf", focus="endurance haute altitude", span_m=155, aspect_ratio=36, payload_kg=700, battery_kg=1700, avionics_kg=36, propulsion_kg=125, fixed_equipment_kg=130, solar_coverage=0.89, cd0=0.017, stealth_alignment=0.34, vertical_tail_factor=0.72, prop_signature_factor=0.68, **common_v3),
        Candidate(code="V3-E", name="Modular Reef", focus="baies payload modulaires", span_m=132, aspect_ratio=30, payload_kg=900, battery_kg=1280, avionics_kg=38, propulsion_kg=105, fixed_equipment_kg=155, solar_coverage=0.84, cd0=0.020, stealth_alignment=0.44, vertical_tail_factor=0.82, prop_signature_factor=0.74, **common_v3),
        Candidate(code="V3-F", name="Lift Fortress", focus="marge structure et portance", span_m=160, aspect_ratio=31, payload_kg=1050, battery_kg=1600, avionics_kg=42, propulsion_kg=130, fixed_equipment_kg=165, solar_coverage=0.86, cd0=0.020, stealth_alignment=0.46, vertical_tail_factor=0.84, prop_signature_factor=0.72, **common_v3),
    ]
    return rows


def stl_tri(name: str, triangles: list[tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]]) -> str:
    lines = [f"solid {name}"]
    for a, b, c in triangles:
        ux, uy, uz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        vx, vy, vz = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        nx, ny, nz = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        norm = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        lines.append(f"  facet normal {nx/norm:.6e} {ny/norm:.6e} {nz/norm:.6e}")
        lines.append("    outer loop")
        for p in (a, b, c):
            lines.append(f"      vertex {p[0]:.6e} {p[1]:.6e} {p[2]:.6e}")
        lines.append("    endloop")
        lines.append("  endfacet")
    lines.append(f"endsolid {name}")
    return "\n".join(lines)


def add_box(triangles: list, cx: float, cy: float, cz: float, lx: float, ly: float, lz: float) -> None:
    x0, x1 = cx - lx / 2, cx + lx / 2
    y0, y1 = cy - ly / 2, cy + ly / 2
    z0, z1 = cz - lz / 2, cz + lz / 2
    v = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    for i, j, k, l in faces:
        triangles.append((v[i], v[j], v[k]))
        triangles.append((v[i], v[k], v[l]))


def add_ellipsoid(triangles: list, cx: float, cy: float, cz: float, rx: float, ry: float, rz: float, seg: int = 28, rings: int = 14) -> None:
    pts = []
    for r in range(rings + 1):
        phi = -math.pi / 2 + math.pi * r / rings
        row = []
        for s in range(seg):
            theta = 2 * math.pi * s / seg
            row.append((cx + rx * math.cos(phi) * math.cos(theta), cy + ry * math.cos(phi) * math.sin(theta), cz + rz * math.sin(phi)))
        pts.append(row)
    for r in range(rings):
        for s in range(seg):
            a = pts[r][s]
            b = pts[r][(s + 1) % seg]
            c = pts[r + 1][(s + 1) % seg]
            d = pts[r + 1][s]
            triangles.append((a, b, c))
            triangles.append((a, c, d))


def add_wing_half(triangles: list, side: int, span_half: float, root_chord: float, tip_chord: float, thickness: float) -> None:
    y0, y1 = 0.0, side * span_half
    zt, zb = thickness / 2, -thickness / 2
    verts = [
        (-0.42 * root_chord, y0, zb),
        (0.58 * root_chord, y0, zb),
        (-0.42 * tip_chord, y1, zb),
        (0.58 * tip_chord, y1, zb),
        (-0.42 * root_chord, y0, zt),
        (0.58 * root_chord, y0, zt),
        (-0.42 * tip_chord, y1, zt),
        (0.58 * tip_chord, y1, zt),
    ]
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    for i, j, k, l in faces:
        triangles.append((verts[i], verts[j], verts[k]))
        triangles.append((verts[i], verts[k], verts[l]))


def make_stl(row: dict[str, Any], path: Path) -> None:
    triangles: list[Any] = []
    add_wing_half(triangles, 1, row["span_m"] / 2, row["root_chord_m"], row["tip_chord_m"], 0.018 * row["root_chord_m"])
    add_wing_half(triangles, -1, row["span_m"] / 2, row["root_chord_m"], row["tip_chord_m"], 0.018 * row["root_chord_m"])
    add_ellipsoid(
        triangles,
        0.10 * row["fuselage_length_m"],
        0,
        -0.04,
        row["fuselage_length_m"] / 2,
        row["fuselage_width_m"] / 2,
        row["fuselage_height_m"] / 2,
    )
    add_box(triangles, 0.08 * row["fuselage_length_m"], 0, -0.08, row["fuselage_length_m"] * 0.34, row["fuselage_width_m"] * 0.82, row["fuselage_height_m"] * 0.38)
    motor_count = 2 if row["version"] != "V3" else 4
    for i in range(motor_count):
        y = (i - (motor_count - 1) / 2) * row["span_m"] / (motor_count + 1)
        add_box(triangles, -0.18 * row["root_chord_m"], y, -0.03, row["root_chord_m"] * 0.18, row["root_chord_m"] * 0.10, row["root_chord_m"] * 0.08)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(stl_tri(row["code"], triangles), encoding="utf-8")


def write_reports(rows: list[dict[str, Any]], selected: list[dict[str, Any]]) -> None:
    REPORTS.mkdir(parents=True, exist_ok=True)
    sources = [
        "Maxeon/SunPower Gen III cells: 23.1-24.3% cell efficiency from ENF/SunPower datasheet.",
        "SolAero UAV triple-junction reference: ~32% UAV solar cell efficiency from UST/SolAero public reference.",
        "Amprius silicon-anode cells: 430-450 Wh/kg commercial references and 500 Wh/kg platform claims.",
        "EMRAX 188: 7.1-7.9 kg, up to 37 kW continuous, 92-98% efficiency from EMRAX public datasheet.",
        "Skytrac DLS-140: Iridium Certus 100, 730 g, 7 W nominal, 18 W peak from SKYTRAC data sheet.",
    ]
    master = [
        "# HALE Solar Glider Concept Study",
        "",
        "## Reformulated Mission",
        "",
        "Design a solar-electric high altitude glider able to carry useful payload, remain at very high altitude, and favor endurance over speed. V1 must target at least 1-2 days in favorable conditions. V1/V2 use carbon plus aluminium/glass where useful; V3 may use titanium-carbon nodes. The study compares 18 candidates, selects 9 polyvalent models, and produces preliminary STL geometry, renders, component topology, energy calculations and relative low-observable scoring.",
        "",
        "## Safety And Fidelity Limits",
        "",
        "- This is conceptual sizing, not certified flight design.",
        "- Wind-tunnel results are analytic lift/drag estimates, not CFD.",
        "- Radar signature is a relative low-observable score, not an operational RCS prediction.",
        "- No weapon payload, targeting logic or radar-evasion recipe is included.",
        "",
        "## Component Sources Used",
        "",
        *[f"- {s}" for s in sources],
        "",
        "## Candidate Ranking",
        "",
        "| Code | Focus | Span m | Payload kg | Gross kg | L/D | Best endurance | Worst endurance | RCS rel | Score |",
        "|---|---|---:|---:|---:|---:|---|---|---:|---:|",
    ]
    for r in sorted(rows, key=lambda x: (x["version"], -x["balanced_score"])):
        master.append(
            f"| {r['code']} {r['name']} | {r['focus']} | {r['span_m']:.1f} | {r['payload_kg']:.0f} | {r['gross_mass_kg']:.0f} | {r['ld']:.1f} | {r['best_endurance_label']} | {r['worst_endurance_label']} | {r['rcs_relative']:.2f} | {r['balanced_score']:.3f} |"
        )
    master.extend(["", "## Selected Aircraft", ""])
    for r in selected:
        master.append(f"- {r['code']} {r['name']}: {r['focus']}, score {r['balanced_score']:.3f}.")
    (REPORTS / "HALE_SOLAR_GLIDER_SYNTHESIS.md").write_text("\n".join(master), encoding="utf-8")

    for r in selected:
        md = [
            f"# {r['code']} - {r['name']}",
            "",
            f"Version: {r['version']}",
            f"Focus: {r['focus']}",
            f"Material: {r['material']}",
            "",
            "## Geometry",
            "",
            f"- Span: {r['span_m']:.2f} m",
            f"- Wing area: {r['wing_area_m2']:.2f} m2",
            f"- Aspect ratio: {r['aspect_ratio']:.1f}",
            f"- Root chord: {r['root_chord_m']:.2f} m",
            f"- Tip chord: {r['tip_chord_m']:.2f} m",
            f"- Fuselage length: {r['fuselage_length_m']:.2f} m",
            f"- Fuselage width/height: {r['fuselage_width_m']:.2f} / {r['fuselage_height_m']:.2f} m",
            "",
            "## Mass Topology",
            "",
            f"- Gross mass: {r['gross_mass_kg']:.1f} kg",
            f"- Payload: {r['payload_kg']:.1f} kg",
            f"- Battery: {r['battery_kg']:.1f} kg",
            f"- Structure: {r['structure_kg']:.1f} kg",
            f"- Solar array: {r['solar_array_kg']:.1f} kg",
            f"- Propulsion: {r['propulsion_kg']:.1f} kg",
            f"- Avionics/comms: {r['avionics_kg']:.1f} kg",
            f"- Wiring/thermal: {r['wiring_thermal_kg']:.1f} kg",
            "",
            "## Components",
            "",
            f"- Motor: {r['motor_choice']}",
            f"- Battery: {r['battery_choice']}",
            f"- Comms: {r['comms_choice']}",
            "- Autopilot: Pixhawk/Cube-class redundant autopilot, dual GNSS, pitot, IMU, barometer, ADS-B/remote ID where legally required.",
            "- Thermal: insulated battery bay, aluminium/carbon heat spreaders, motor-controller waste heat conducted through a regulated loop.",
            "",
            "## Wind-Tunnel Proxy",
            "",
            f"- Altitude: {r['altitude_m']:.0f} m",
            f"- Air density: {r['density_kg_m3']:.4f} kg/m3",
            f"- Cruise speed: {r['cruise_speed_mps']:.2f} m/s",
            f"- CL/CD: {r['cl']:.3f} / {r['cd']:.4f}",
            f"- L/D: {r['ld']:.2f}",
            f"- Drag: {r['drag_n']:.1f} N",
            f"- Wing loading: {r['wing_loading_kg_m2']:.2f} kg/m2",
            f"- Structural margin proxy: {r['structural_margin_proxy']:.2f}",
            "",
            "## Energy And Endurance",
            "",
            f"- Solar area: {r['solar_area_m2']:.2f} m2",
            f"- Battery usable energy: {r['battery_usable_kwh']:.2f} kWh",
            f"- Best solar harvest/day: {r['solar_best_kwh_day']:.2f} kWh",
            f"- Worst solar harvest/day: {r['solar_worst_kwh_day']:.2f} kWh",
            f"- Best cruise demand: {r['demand_best_kw']:.2f} kW",
            f"- Worst cruise demand: {r['demand_worst_kw']:.2f} kW",
            f"- Best endurance: {r['best_endurance_label']}",
            f"- Worst endurance: {r['worst_endurance_label']}",
            f"- Best solar daily margin: {r['solar_margin_best']:.2f}",
            f"- Worst solar daily margin: {r['solar_margin_worst']:.2f}",
            "",
            "## Battery Heating Concept",
            "",
            f"- Motor waste heat available: {r['motor_waste_w']:.0f} W",
            f"- Conducted heat estimate: {r['conducted_heat_w']:.0f} W",
            f"- Battery bay heat need proxy: {r['battery_heat_need_w']:.0f} W",
            f"- Extra electric heater in worst case: {r['heater_extra_w']:.0f} W",
            "",
            "## Low-Observable Relative Assessment",
            "",
            f"- Relative signature score: {r['rcs_score']:.1f}/100",
            f"- Relative RCS proxy: {r['rcs_relative']:.3f}",
            "- Design choices: blended fuselage, internal payload bay, pusher propulsion, reduced vertical surfaces, planform alignment.",
            "",
            "## Files",
            "",
            f"- STL: artifacts/hale_solar_glider/models/{r['code']}_{r['name'].replace(' ', '_')}.stl",
            f"- Render ISO: artifacts/hale_solar_glider/renders/{r['code']}_iso.png",
            f"- Render top: artifacts/hale_solar_glider/renders/{r['code']}_top.png",
            f"- Render front: artifacts/hale_solar_glider/renders/{r['code']}_front.png",
        ]
        (REPORTS / f"{r['code']}_{r['name'].replace(' ', '_')}.md").write_text("\n".join(md), encoding="utf-8")


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    rows = [compute(c) for c in candidates()]
    for row in rows:
        row["selected"] = False
    selected_codes = {"V1-A", "V1-C", "V1-D", "V2-C", "V2-D", "V2-F", "V3-A", "V3-C", "V3-D"}
    selected: list[dict[str, Any]] = []
    for row in rows:
        if row["code"] in selected_codes:
            row["selected"] = True
            selected.append(row)
    (OUT / "hale_candidates.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    (OUT / "hale_selected.json").write_text(json.dumps(selected, indent=2), encoding="utf-8")
    fields = [
        "code",
        "name",
        "version",
        "focus",
        "span_m",
        "payload_kg",
        "gross_mass_kg",
        "wing_area_m2",
        "ld",
        "demand_best_kw",
        "demand_worst_kw",
        "battery_usable_kwh",
        "solar_best_kwh_day",
        "solar_worst_kwh_day",
        "best_endurance_h",
        "worst_endurance_h",
        "rcs_relative",
        "balanced_score",
        "selected",
    ]
    with (OUT / "hale_candidates.csv").open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in fields})
    source_notes = {
        "schema": "hesia.hale.source_assumptions.v1",
        "sources": {
            "maxeon_gen_iii": "https://www.enfsolar.com/pv/cell-datasheet/1740",
            "sunpower_maxeon_3_datasheet": "https://sunpower.maxeon.com/int/sites/default/files/2022-05/sp_max3_104c_com_380-400_dc_ds_en_a4_544454.pdf",
            "solaero_uav_cells": "https://www.unmannedsystemstechnology.com/2018/06/solaero-technologies-develops-high-efficiency-solar-cells-and-panels-for-uavs/",
            "amprius_silicon_anode": "https://amprius.com/our-solutions/simaxx/",
            "emrax_188": "https://emrax.com/e-motors/emrax-188/",
            "skytrac_dls140": "https://www.skytrac.ca/wp-content/uploads/2024/10/STS-Data-Sheet-DLS-140-CA-EN-2024-R02.pdf",
        },
        "simulation_limit": "First-order analytic sizing only; CFD/RCS values are relative proxies.",
    }
    (OUT / "source_assumptions.json").write_text(json.dumps(source_notes, indent=2), encoding="utf-8")
    for row in selected:
        make_stl(row, MODELS / f"{row['code']}_{row['name'].replace(' ', '_')}.stl")
    write_reports(rows, selected)
    print(json.dumps({"candidates": len(rows), "selected": len(selected), "out": str(OUT)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
