#!/usr/bin/env python3
import os
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    try:
        import webview  # type: ignore
    except Exception:
        print("Missing dependency: pywebview")
        print("Install with: python -m pip install pywebview")
        return 1

    base_dir = Path(__file__).resolve().parent.parent
    ui_port = int(os.environ.get("HESIA_UI_PORT", "8080"))
    ui_width = int(os.environ.get("HESIA_UI_WIDTH", "1280"))
    ui_height = int(os.environ.get("HESIA_UI_HEIGHT", "720"))
    server_cmd = [sys.executable, str(base_dir / "tools" / "ui_server.py")]

    proc = subprocess.Popen(server_cmd, env=os.environ.copy())
    time.sleep(0.5)

    url = f"http://127.0.0.1:{ui_port}"
    webview.create_window("HESIA Console", url, width=ui_width, height=ui_height)
    try:
        webview.start()
    finally:
        proc.terminate()
        proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
