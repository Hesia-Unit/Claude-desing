#!/usr/bin/env python3
"""Smoke/fuzz PLFM bridge input validation without requiring hardware."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


CASES = {
    "valid": [
        {
            "timestamp_ns": "0",
            "chirp_number": "0",
            "chirp_type": "LONG",
            "sample_index": "12",
            "I_value": "3",
            "Q_value": "4",
            "magnitude_squared": "1000",
        }
    ],
    "nan": [
        {
            "timestamp_ns": "0",
            "chirp_number": "0",
            "chirp_type": "LONG",
            "sample_index": "1",
            "I_value": "0",
            "Q_value": "0",
            "magnitude_squared": "nan",
        }
    ],
    "inf": [
        {
            "timestamp_ns": "0",
            "chirp_number": "0",
            "chirp_type": "LONG",
            "sample_index": "1",
            "I_value": "0",
            "Q_value": "0",
            "magnitude_squared": "inf",
        }
    ],
    "out_of_range": [
        {
            "timestamp_ns": "0",
            "chirp_number": "0",
            "chirp_type": "LONG",
            "sample_index": "999999",
            "I_value": "0",
            "Q_value": "0",
            "magnitude_squared": "1000",
        }
    ],
    "missing": [
        {
            "timestamp_ns": "0",
            "chirp_number": "0",
            "chirp_type": "LONG",
            "sample_index": "",
            "I_value": "0",
            "Q_value": "0",
            "magnitude_squared": "1000",
        }
    ],
}


def write_case(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["timestamp_ns", "chirp_number", "chirp_type", "sample_index", "I_value", "Q_value", "magnitude_squared"]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--bridge", type=Path, default=Path("tools/plfm_to_hesia_bridge.py"))
    parser.add_argument("--python", default="python")
    args = parser.parse_args()

    args.workdir.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, object]] = []
    for name, rows in CASES.items():
        input_path = args.workdir / f"{name}.csv"
        output_path = args.workdir / f"{name}.jsonl"
        summary_path = args.workdir / f"{name}.summary.json"
        write_case(input_path, rows)
        proc = subprocess.run(
            [
                args.python,
                str(args.bridge),
                "--input",
                str(input_path),
                "--output",
                str(output_path),
                "--summary",
                str(summary_path),
                "--threshold",
                "10",
                "--max-detections",
                "4",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        summary = json.loads(summary_path.read_text(encoding="utf-8")) if summary_path.exists() else {}
        results.append(
            {
                "case": name,
                "exit_code": proc.returncode,
                "detection_count": summary.get("summary", {}).get("detection_count"),
                "rejected_rows": summary.get("summary", {}).get("rejected_rows"),
            }
        )

    expectations = {
        "valid": (0, 1, 0),
        "nan": (0, 0, 1),
        "inf": (0, 0, 1),
        "out_of_range": (0, 0, 1),
        "missing": (0, 0, 1),
    }
    for result in results:
        expected = expectations[str(result["case"])]
        observed = (result["exit_code"], result["detection_count"], result["rejected_rows"])
        if observed != expected:
            raise SystemExit(f"unexpected result for {result['case']}: {observed} != {expected}")

    print(json.dumps({"status": "passed", "cases": results}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
