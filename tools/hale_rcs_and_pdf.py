#!/usr/bin/env python3
"""Generate relative RCS plan images and image-rich PDFs for HALE candidates."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt
import numpy as np
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Image, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "hale_solar_glider"
RENDERS = OUT / "renders"
REPORTS = OUT / "reports"
RCS = OUT / "rcs_plans"
AERO = OUT / "aero_plots"


def esc(text: Any) -> str:
    return str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def signature_curve(row: dict[str, Any], scenario: str) -> tuple[np.ndarray, np.ndarray]:
    theta = np.linspace(0, 2 * np.pi, 361)
    base = row["rcs_relative"]
    if scenario == "best":
        frontal = 0.42
        beam = 0.70
        tail = 0.55
        jitter = 0.03
    else:
        frontal = 1.35
        beam = 2.15
        tail = 1.05
        jitter = 0.12
    lobes = (
        frontal * np.cos(theta) ** 8
        + tail * np.cos(theta - np.pi) ** 8
        + beam * (np.sin(theta) ** 10)
        + 0.18
    )
    alignment = row["stealth_alignment"]
    curve = base * lobes * (0.72 + alignment) * (1.0 + jitter * np.sin(6 * theta))
    return theta, np.clip(curve, 0.02, None)


def make_rcs_plan(row: dict[str, Any]) -> Path:
    RCS.mkdir(parents=True, exist_ok=True)
    fig = plt.figure(figsize=(11, 5.5), dpi=170)
    fig.suptitle(f"{row['code']} {row['name']} - relative fuselage RCS plan", fontsize=13, fontweight="bold")
    for idx, scenario in enumerate(("best", "worst"), start=1):
        ax = fig.add_subplot(1, 2, idx, projection="polar")
        theta, curve = signature_curve(row, scenario)
        ax.plot(theta, curve, linewidth=2.2, color="#167a6f" if scenario == "best" else "#a33a2f")
        ax.fill(theta, curve, alpha=0.22, color="#167a6f" if scenario == "best" else "#a33a2f")
        ax.set_theta_zero_location("N")
        ax.set_theta_direction(-1)
        ax.set_title("Best case: nose/edge alignment" if scenario == "best" else "Worst case: broadside/aspect change", fontsize=10)
        ax.grid(True, alpha=0.35)
        ax.set_yticklabels([])
        ax.annotate("nose", xy=(0, curve[0]), xytext=(0.12, max(curve) * 0.95), fontsize=8)
        ax.annotate("broadside", xy=(math.pi / 2, np.interp(math.pi / 2, theta, curve)), xytext=(math.pi / 2, max(curve) * 0.85), fontsize=8)
    fig.text(
        0.5,
        0.02,
        "Relative conceptual score only. It compares planform/fuselage exposure between candidates; it is not an operational or certified radar-cross-section prediction.",
        ha="center",
        fontsize=8,
    )
    out = RCS / f"{row['code']}_rcs_plan.png"
    fig.tight_layout(rect=(0, 0.05, 1, 0.95))
    fig.savefig(out)
    plt.close(fig)
    return out


def make_aero_plot(row: dict[str, Any]) -> Path:
    AERO.mkdir(parents=True, exist_ok=True)
    rho = row["density_kg_m3"]
    wing_area = row["wing_area_m2"]
    weight = row["gross_mass_kg"] * 9.80665
    aspect_ratio = row["aspect_ratio"]
    oswald = row["oswald"]
    cd0 = row["cd0"]
    prop_efficiency = row["prop_efficiency"]
    speeds = np.linspace(max(16.0, row["cruise_speed_mps"] * 0.55), row["cruise_speed_mps"] * 1.75, 140)
    cl_required = weight / (0.5 * rho * speeds**2 * wing_area)
    cd = cd0 + cl_required**2 / (math.pi * oswald * aspect_ratio)
    drag = 0.5 * rho * speeds**2 * wing_area * cd
    shaft_power_kw = drag * speeds / max(prop_efficiency, 0.1) / 1000.0
    cl_max_clean = 1.15
    cl_max_flap = 1.45

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.6), dpi=170)
    fig.suptitle(f"{row['code']} {row['name']} - analytic lift/drag tunnel proxy", fontsize=13, fontweight="bold")
    axes[0].plot(speeds, cl_required, color="#1f6f8b", linewidth=2.2, label="CL required")
    axes[0].axhline(cl_max_clean, color="#d48a00", linestyle="--", linewidth=1.5, label="CL max clean proxy")
    axes[0].axhline(cl_max_flap, color="#6a994e", linestyle=":", linewidth=1.8, label="CL max flap proxy")
    axes[0].axvline(row["cruise_speed_mps"], color="#333333", linestyle="-.", linewidth=1.2, label="cruise")
    axes[0].set_xlabel("Speed (m/s)")
    axes[0].set_ylabel("Lift coefficient")
    axes[0].grid(True, alpha=0.3)
    axes[0].legend(fontsize=7)

    axes[1].plot(speeds, drag, color="#8f2d56", linewidth=2.0, label="Drag (N)")
    axes[1].set_xlabel("Speed (m/s)")
    axes[1].set_ylabel("Drag (N)", color="#8f2d56")
    axes[1].tick_params(axis="y", labelcolor="#8f2d56")
    axes[1].grid(True, alpha=0.3)
    ax2 = axes[1].twinx()
    ax2.plot(speeds, shaft_power_kw, color="#2a9d8f", linewidth=2.0, label="Shaft power (kW)")
    ax2.set_ylabel("Shaft power (kW)", color="#2a9d8f")
    ax2.tick_params(axis="y", labelcolor="#2a9d8f")
    axes[1].axvline(row["cruise_speed_mps"], color="#333333", linestyle="-.", linewidth=1.2)
    fig.text(
        0.5,
        0.02,
        "First-order sweep only: atmosphere, rigid geometry and induced/profile drag approximations; not a CFD or certified wind-tunnel result.",
        ha="center",
        fontsize=8,
    )
    out = AERO / f"{row['code']}_aero_proxy.png"
    fig.tight_layout(rect=(0, 0.06, 1, 0.93))
    fig.savefig(out)
    plt.close(fig)
    return out


def styles():
    s = getSampleStyleSheet()
    s.add(ParagraphStyle("Small", parent=s["BodyText"], fontSize=8.5, leading=11))
    s.add(ParagraphStyle("Body", parent=s["BodyText"], fontSize=9.3, leading=12.5))
    s["Title"].fontSize = 18
    s["Heading2"].fontSize = 13
    return s


def table(data: list[list[Any]]) -> Table:
    t = Table([[Paragraph(esc(c), styles()["Small"]) for c in row] for row in data], repeatRows=1)
    t.setStyle(
        TableStyle(
            [
                ("GRID", (0, 0), (-1, -1), 0.25, colors.lightgrey),
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#edf2f4")),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 4),
                ("RIGHTPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    return t


def add_image(story: list[Any], path: Path, width_mm: float) -> None:
    if path.exists():
        story.append(Image(str(path), width=width_mm * mm, height=width_mm * mm * 0.55))
        story.append(Spacer(1, 3 * mm))


def make_pdf(row: dict[str, Any]) -> Path:
    st = styles()
    pdf = REPORTS / f"{row['code']}_{row['name'].replace(' ', '_')}_COMPLETE.pdf"
    story: list[Any] = [
        Paragraph(f"{row['code']} - {esc(row['name'])}", st["Title"]),
        Paragraph(f"{row['version']} | {esc(row['focus'])}", st["Body"]),
        Spacer(1, 4 * mm),
        Paragraph("Mission Fit", st["Heading2"]),
        Paragraph(
            "Solar-electric high altitude glider concept sized for payload carriage, long endurance and reduced relative signature. This document is conceptual sizing, not certified flight release.",
            st["Body"],
        ),
        Spacer(1, 3 * mm),
        table(
            [
                ["Item", "Value"],
                ["Material", row["material"]],
                ["Span", f"{row['span_m']:.1f} m"],
                ["Payload", f"{row['payload_kg']:.0f} kg"],
                ["Gross mass", f"{row['gross_mass_kg']:.0f} kg"],
                ["Altitude design point", f"{row['altitude_m']:.0f} m"],
                ["L/D proxy", f"{row['ld']:.1f}"],
                ["Best endurance", row["best_endurance_label"]],
                ["Worst endurance", row["worst_endurance_label"]],
            ]
        ),
        Spacer(1, 4 * mm),
        Paragraph("Rendered Geometry", st["Heading2"]),
    ]
    for view in ("iso", "top", "front"):
        add_image(story, RENDERS / f"{row['code']}_{view}.png", 170)
    story.extend(
        [
            Paragraph("Mass And Components", st["Heading2"]),
            table(
                [
                    ["Subsystem", "Mass / Selection"],
                    ["Structure", f"{row['structure_kg']:.1f} kg"],
                    ["Solar array", f"{row['solar_array_kg']:.1f} kg over {row['solar_area_m2']:.1f} m2"],
                    ["Battery", f"{row['battery_kg']:.1f} kg, {row['battery_choice']}"],
                    ["Propulsion", f"{row['propulsion_kg']:.1f} kg, {row['motor_choice']}"],
                    ["Comms", row["comms_choice"]],
                    ["Payload bay", f"{row['payload_kg']:.1f} kg target useful payload"],
                ]
            ),
            Spacer(1, 4 * mm),
            Paragraph("Analytic Wind-Tunnel Proxy", st["Heading2"]),
            table(
                [
                    ["Metric", "Value"],
                    ["Cruise speed", f"{row['cruise_speed_mps']:.1f} m/s"],
                    ["CL / CD", f"{row['cl']:.3f} / {row['cd']:.4f}"],
                    ["Drag", f"{row['drag_n']:.0f} N"],
                    ["Wing loading", f"{row['wing_loading_kg_m2']:.2f} kg/m2"],
                    ["Structural margin proxy", f"{row['structural_margin_proxy']:.2f}"],
                ]
            ),
            Spacer(1, 4 * mm),
            Paragraph("Lift And Drag Sweep", st["Heading2"]),
            Paragraph(
                "The following plot sweeps speed around the cruise point and checks required lift coefficient, a clean/flap stall proxy, drag and shaft power. It is a low-order analytic tunnel surrogate for concept selection.",
                st["Body"],
            ),
        ]
    )
    add_image(story, AERO / f"{row['code']}_aero_proxy.png", 175)
    story.extend(
        [
            Paragraph("Energy, Solar And Heating", st["Heading2"]),
            table(
                [
                    ["Metric", "Value"],
                    ["Battery usable energy", f"{row['battery_usable_kwh']:.1f} kWh"],
                    ["Best solar harvest/day", f"{row['solar_best_kwh_day']:.1f} kWh"],
                    ["Worst solar harvest/day", f"{row['solar_worst_kwh_day']:.1f} kWh"],
                    ["Best / worst demand", f"{row['demand_best_kw']:.2f} / {row['demand_worst_kw']:.2f} kW"],
                    ["Motor waste heat", f"{row['motor_waste_w']:.0f} W"],
                    ["Conducted heat to battery bay", f"{row['conducted_heat_w']:.0f} W"],
                    ["Extra heater worst case", f"{row['heater_extra_w']:.0f} W"],
                ]
            ),
            Spacer(1, 4 * mm),
            Paragraph("Fuselage RCS Plan - Best And Worst Cases", st["Heading2"]),
            Paragraph(
                "The following plot is a relative fuselage/planform exposure diagram. Best case assumes nose/edge alignment and folded or masked propulsion; worst case assumes broadside exposure, aspect change and higher propulsor contribution.",
                st["Body"],
            ),
        ]
    )
    add_image(story, RCS / f"{row['code']}_rcs_plan.png", 175)
    story.extend(
        [
            table(
                [
                    ["Metric", "Value"],
                    ["Relative RCS proxy", f"{row['rcs_relative']:.3f}"],
                    ["Relative signature score", f"{row['rcs_score']:.1f}/100"],
                    ["Stealth alignment factor", f"{row['stealth_alignment']:.2f}"],
                    ["Vertical tail factor", f"{row['vertical_tail_factor']:.2f}"],
                    ["Prop signature factor", f"{row['prop_signature_factor']:.2f}"],
                ]
            ),
            Spacer(1, 4 * mm),
            Paragraph("Files", st["Heading2"]),
            Paragraph(f"STL: artifacts/hale_solar_glider/models/{row['code']}_{row['name'].replace(' ', '_')}.stl", st["Small"]),
            Paragraph(f"OBJ: artifacts/hale_solar_glider/obj/{row['code']}_{row['name'].replace(' ', '_')}.obj", st["Small"]),
            Paragraph(f"RCS plan: artifacts/hale_solar_glider/rcs_plans/{row['code']}_rcs_plan.png", st["Small"]),
        ]
    )
    REPORTS.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(str(pdf), pagesize=A4, rightMargin=14 * mm, leftMargin=14 * mm, topMargin=13 * mm, bottomMargin=13 * mm)
    doc.build(story)
    return pdf


def make_synthesis(rows: list[dict[str, Any]], selected: list[dict[str, Any]]) -> Path:
    st = styles()
    pdf = REPORTS / "HALE_SOLAR_GLIDER_SYNTHESIS_COMPLETE.pdf"
    selected_codes = {row["code"] for row in selected}
    sources = json.loads((OUT / "source_assumptions.json").read_text(encoding="utf-8"))
    story: list[Any] = [
        Paragraph("HALE Solar Glider Concept Study", st["Title"]),
        Paragraph("18 candidates generated; 9 selected across V1/V2/V3. Each selected aircraft has a dedicated PDF, STL geometry, Blender renders and a best/worst-case relative fuselage RCS plan.", st["Body"]),
        Spacer(1, 4 * mm),
        Paragraph("Selected Concepts", st["Heading2"]),
        table(
            [["Code", "Focus", "Span", "Payload", "Gross", "L/D", "Best", "Worst", "RCS rel", "PDF"]]
            + [
                [
                    row["code"],
                    row["focus"],
                    f"{row['span_m']:.0f} m",
                    f"{row['payload_kg']:.0f} kg",
                    f"{row['gross_mass_kg']:.0f} kg",
                    f"{row['ld']:.1f}",
                    row["best_endurance_label"],
                    row["worst_endurance_label"],
                    f"{row['rcs_relative']:.2f}",
                    f"{row['code']}_{row['name'].replace(' ', '_')}_COMPLETE.pdf",
                ]
                for row in selected
            ]
        ),
        Spacer(1, 4 * mm),
        Paragraph("Method", st["Heading2"]),
        Paragraph(
            "The sizing loop uses first-order flight mechanics: span/aspect-ratio geometry, high-altitude density, lift coefficient target, induced/profile drag, propulsion efficiency, battery usable energy, solar area and solar irradiance envelopes. Best case assumes strong daylight and nominal propulsion demand; worst case assumes reduced daylight/irradiance, higher propulsion demand and thermal battery support.",
            st["Body"],
        ),
        Spacer(1, 3 * mm),
        Paragraph(
            "RCS outputs are relative planform/fuselage exposure proxies only. They are intended to compare concepts and highlight nose/edge alignment versus broadside/aspect risk. They are not certified dBsm predictions and should not be treated as operational radar-evasion engineering.",
            st["Body"],
        ),
        Spacer(1, 4 * mm),
        Paragraph("All 18 Candidates", st["Heading2"]),
        table(
            [["Code", "Selected", "Focus", "Span", "Payload", "Gross", "Best", "Worst", "RCS rel"]]
            + [
                [
                    row["code"],
                    "yes" if row["code"] in selected_codes else "no",
                    row["focus"],
                    f"{row['span_m']:.0f} m",
                    f"{row['payload_kg']:.0f} kg",
                    f"{row['gross_mass_kg']:.0f} kg",
                    row["best_endurance_label"],
                    row["worst_endurance_label"],
                    f"{row['rcs_relative']:.2f}",
                ]
                for row in rows
            ]
        ),
        PageBreak(),
        Paragraph("Engineering Notes", st["Heading2"]),
        table(
            [
                ["Topic", "Result"],
                ["V1", "Best-case endurance reaches about one day for the selected V1 concepts, but the harsh worst case remains below one day."],
                ["V2", "V2-C/V2-D are the most credible medium-size HALE concepts. V2-F is retained for lift/payload study despite weak endurance."],
                ["V3", "V3-C/V3-D recover about one day in best case with large payload potential; structural and ground-handling risk dominate."],
                ["Heating", "Battery bay heating is treated as a conduction-assisted thermal balance from motor waste heat plus optional heater load."],
                ["Materials", "V1/V2 use carbon plus aluminium/fiberglass inserts; V3 moves toward titanium/carbon where bending loads become dominant."],
            ]
        ),
        Spacer(1, 4 * mm),
        Paragraph("Notes", st["Heading2"]),
        Paragraph(
            "Worst case is intentionally harsh: shorter daylight, lower irradiance, +18% propulsion demand and battery thermal penalty. Several V1 concepts reach about one day in best case, but not in worst case; this is a design risk rather than a hidden success.",
            st["Body"],
        ),
        Spacer(1, 4 * mm),
        Paragraph("Source Assumptions", st["Heading2"]),
        table(
            [["Reference", "URL"]]
            + [[key, value] for key, value in sources["sources"].items()]
            + [["Simulation limit", sources["simulation_limit"]]]
        ),
    ]
    doc = SimpleDocTemplate(str(pdf), pagesize=A4, rightMargin=14 * mm, leftMargin=14 * mm, topMargin=13 * mm, bottomMargin=13 * mm)
    doc.build(story)
    return pdf


def main() -> int:
    rows = json.loads((OUT / "hale_candidates.json").read_text(encoding="utf-8"))
    selected = json.loads((OUT / "hale_selected.json").read_text(encoding="utf-8"))
    for row in selected:
        make_rcs_plan(row)
        make_aero_plot(row)
        make_pdf(row)
    make_synthesis(rows, selected)
    print(json.dumps({"selected": len(selected), "reports": str(REPORTS), "rcs": str(RCS)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
