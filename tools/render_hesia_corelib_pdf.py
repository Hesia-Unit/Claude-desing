#!/usr/bin/env python3
"""Render HESIA CoreLib markdown references to simple PDF handover files."""

from __future__ import annotations

import argparse
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Paragraph, Preformatted, SimpleDocTemplate, Spacer, Table, TableStyle


def inline_markup(text: str) -> str:
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return text.replace("`", "")


def paragraph_for(line: str, styles):
    stripped = line.strip()
    if not stripped:
        return Spacer(1, 3 * mm)
    if stripped.startswith("# "):
        return Paragraph(inline_markup(stripped[2:]), styles["Title"])
    if stripped.startswith("## "):
        return Paragraph(inline_markup(stripped[3:]), styles["Heading2"])
    if stripped.startswith("### "):
        return Paragraph(inline_markup(stripped[4:]), styles["Heading3"])
    if stripped.startswith("- "):
        return Paragraph("&bull; " + inline_markup(stripped[2:]), styles["HesiaBullet"])
    if stripped[0:3].isdigit() and stripped[3:5] == ". ":
        return Paragraph(inline_markup(stripped), styles["Body"])
    if stripped == "---":
        return Spacer(1, 4 * mm)
    return Paragraph(inline_markup(stripped), styles["Body"])


def render_markdown(markdown_path: Path, pdf_path: Path) -> None:
    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle("Body", parent=styles["BodyText"], fontName="Helvetica", fontSize=9.5, leading=13))
    styles.add(ParagraphStyle("HesiaBullet", parent=styles["Body"], leftIndent=8 * mm, firstLineIndent=-4 * mm))
    styles["Title"].fontName = "Helvetica-Bold"
    styles["Title"].fontSize = 20
    styles["Title"].leading = 24
    styles["Heading2"].fontName = "Helvetica-Bold"
    styles["Heading2"].fontSize = 14
    styles["Heading2"].leading = 18
    styles["Heading3"].fontName = "Helvetica-Bold"
    styles["Heading3"].fontSize = 11
    styles["Heading3"].leading = 14

    story = []
    in_code = False
    code_lines: list[str] = []
    in_table = False
    table_rows: list[list[str]] = []

    def flush_code():
        nonlocal code_lines
        if code_lines:
            story.append(Preformatted("\n".join(code_lines), styles["Code"]))
            story.append(Spacer(1, 2 * mm))
            code_lines = []

    def flush_table():
        nonlocal table_rows, in_table
        if table_rows:
            cleaned = []
            for row in table_rows:
                if all(set(cell.strip()) <= {"-", ":"} for cell in row):
                    continue
                cleaned.append([Paragraph(inline_markup(cell.strip()), styles["Body"]) for cell in row])
            if cleaned:
                table = Table(cleaned, repeatRows=1)
                table.setStyle(
                    TableStyle(
                        [
                            ("GRID", (0, 0), (-1, -1), 0.25, colors.lightgrey),
                            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#f1f3f5")),
                            ("VALIGN", (0, 0), (-1, -1), "TOP"),
                            ("LEFTPADDING", (0, 0), (-1, -1), 4),
                            ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                        ]
                    )
                )
                story.append(table)
                story.append(Spacer(1, 3 * mm))
        table_rows = []
        in_table = False

    for raw_line in markdown_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        if line.startswith("```"):
            if in_code:
                flush_code()
                in_code = False
            else:
                flush_table()
                in_code = True
            continue
        if in_code:
            code_lines.append(line)
            continue
        if line.strip().startswith("|") and line.strip().endswith("|"):
            in_table = True
            table_rows.append([cell for cell in line.strip().strip("|").split("|")])
            continue
        if in_table:
            flush_table()
        story.append(paragraph_for(line, styles))

    flush_code()
    flush_table()

    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(
        str(pdf_path),
        pagesize=A4,
        rightMargin=18 * mm,
        leftMargin=18 * mm,
        topMargin=16 * mm,
        bottomMargin=16 * mm,
        title=markdown_path.stem,
    )
    doc.build(story)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()
    for md_path in args.inputs:
        render_markdown(md_path, md_path.with_suffix(".pdf"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
