#!/usr/bin/env python3
"""Create a desktop-backed hardware mirror for Jetson-Ajax simulation."""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def run_text(command: list[str], timeout: int = 15) -> tuple[int, str]:
    try:
        proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
        return proc.returncode, (proc.stdout + proc.stderr).strip()
    except Exception as exc:
        return 1, f"{type(exc).__name__}: {exc}"


def powershell_json(script: str) -> Any | None:
    code, text = run_text(["powershell", "-NoProfile", "-Command", script])
    if code != 0 or not text:
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")


def get_cpu_info() -> dict[str, Any]:
    data = powershell_json(
        "Get-CimInstance Win32_Processor | "
        "Select-Object Name,CurrentClockSpeed,MaxClockSpeed,NumberOfCores,NumberOfLogicalProcessors | "
        "ConvertTo-Json -Compress"
    )
    if isinstance(data, list):
        data = data[0] if data else None
    if not isinstance(data, dict):
        return {"status": "unavailable", "source": "Win32_Processor", "error": "CIM query failed"}
    return {
        "status": "desktop_backed",
        "source": "Win32_Processor",
        "name": data.get("Name"),
        "current_clock_mhz": data.get("CurrentClockSpeed"),
        "max_clock_mhz": data.get("MaxClockSpeed"),
        "cores": data.get("NumberOfCores"),
        "logical_processors": data.get("NumberOfLogicalProcessors"),
    }


def get_voltage_info() -> dict[str, Any]:
    data = powershell_json(
        "Get-CimInstance Win32_VoltageProbe -ErrorAction SilentlyContinue | "
        "Select-Object Name,CurrentReading,NominalReading,MinReadable,MaxReadable | "
        "ConvertTo-Json -Compress"
    )
    if not data:
        return {
            "status": "unavailable",
            "source": "Win32_VoltageProbe",
            "reason": "no reliable CPU voltage sensor was exposed by Windows CIM",
            "numeric_sysfs_path_created": False,
        }
    return {
        "status": "unavailable",
        "source": "Win32_VoltageProbe",
        "raw": data,
        "reason": "voltage probe exists, but no CPU rail mapping is assumed without a board-specific label",
        "numeric_sysfs_path_created": False,
    }


def get_battery_info() -> dict[str, Any]:
    data = powershell_json(
        "Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue | "
        "Select-Object Name,EstimatedChargeRemaining,BatteryStatus | ConvertTo-Json -Compress"
    )
    if not data:
        return {"status": "unavailable", "source": "Win32_Battery", "reason": "desktop has no exposed battery object"}
    return {"status": "desktop_backed", "source": "Win32_Battery", "raw": data}


def get_wsl_value(path: str) -> dict[str, Any]:
    code, text = run_text(["wsl", "-e", "bash", "-lc", f"cat {path!r}"], timeout=10)
    if code == 0 and text:
        return {"status": "desktop_backed", "source": f"WSL:{path}", "value": text.splitlines()[0].strip()}
    return {"status": "unavailable", "source": f"WSL:{path}", "error": text}


def get_gpu_info() -> dict[str, Any]:
    query = "name,temperature.gpu,power.draw,clocks.current.graphics,clocks.current.memory,memory.used,memory.total"
    code, text = run_text(["nvidia-smi", f"--query-gpu={query}", "--format=csv,noheader,nounits"], timeout=15)
    if code != 0 or not text:
        return {"status": "unavailable", "source": "nvidia-smi", "error": text}
    fields = [part.strip() for part in text.splitlines()[0].split(",")]
    keys = [
        "name",
        "temperature_c",
        "power_w",
        "graphics_clock_mhz",
        "memory_clock_mhz",
        "memory_used_mib",
        "memory_total_mib",
    ]
    return {"status": "desktop_backed", "source": "nvidia-smi", **dict(zip(keys, fields))}


def render_cpuinfo(cpu: dict[str, Any]) -> str:
    logical = int(cpu.get("logical_processors") or os.cpu_count() or 1)
    mhz = float(cpu.get("current_clock_mhz") or 0.0)
    name = cpu.get("name") or platform.processor() or "desktop CPU"
    blocks: list[str] = []
    for idx in range(logical):
        blocks.append(
            "\n".join(
                [
                    f"processor\t: {idx}",
                    "vendor_id\t: desktop-backed",
                    f"model name\t: {name}",
                    f"cpu MHz\t\t: {mhz:.3f}",
                    "source\t\t: Win32_Processor.CurrentClockSpeed",
                ]
            )
        )
    return "\n\n".join(blocks) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path(r"F:\Jetson-Ajax\sys_mirror"))
    parser.add_argument(
        "--report",
        type=Path,
        default=Path(r"C:\Users\matis\Documents\Hesia-Firmware\artifacts\jetson_ajax\phase4_hardware_mirror_snapshot.json"),
    )
    args = parser.parse_args()

    output = args.output
    for subdir in (
        output / "proc",
        output / "sys" / "devices" / "system" / "clocksource" / "clocksource0",
        output / "sys" / "class" / "hwmon",
        output / "metadata",
    ):
        subdir.mkdir(parents=True, exist_ok=True)

    cpu = get_cpu_info()
    voltage = get_voltage_info()
    battery = get_battery_info()
    gpu = get_gpu_info()
    clocksource = get_wsl_value("/sys/devices/system/clocksource/clocksource0/current_clocksource")
    uptime = get_wsl_value("/proc/uptime")

    write_text(output / "proc" / "cpuinfo", render_cpuinfo(cpu) if cpu.get("status") == "desktop_backed" else "")
    write_text(output / "proc" / "uptime", (uptime.get("value") or f"{time.monotonic():.2f} 0.00") + "\n")
    write_text(
        output / "sys" / "devices" / "system" / "clocksource" / "clocksource0" / "current_clocksource",
        (clocksource.get("value") or "unavailable") + "\n",
    )

    snapshot = {
        "created_at": datetime.now(timezone.utc).isoformat(),
        "output": str(output),
        "policy": {
            "no_fake_voltage": True,
            "unavailable_values_are_explicit": True,
            "desktop_backed_values_include_source": True,
        },
        "cpu": cpu,
        "voltage": voltage,
        "battery": battery,
        "gpu": gpu,
        "clocksource": clocksource,
        "uptime": uptime,
        "paths": {
            "HESIA_CPUINFO_PATH": str(output / "proc" / "cpuinfo"),
            "HESIA_UPTIME_PATH": str(output / "proc" / "uptime"),
            "HESIA_CLOCK_SOURCE_PATH": str(
                output / "sys" / "devices" / "system" / "clocksource" / "clocksource0" / "current_clocksource"
            ),
            "HESIA_VOLTAGE_SENSOR_PATH": None,
        },
    }
    write_json(output / "metadata" / "desktop_hardware_snapshot.json", snapshot)
    write_json(args.report, snapshot)
    print(json.dumps(snapshot, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
