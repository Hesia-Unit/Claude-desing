#!/usr/bin/env python3
import json
import os
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional
from pathlib import Path
from urllib.parse import urlparse, parse_qs

BASE_DIR = Path(__file__).resolve().parent.parent
UI_DIR = Path(os.environ.get("HESIA_UI_STATIC", BASE_DIR / "ui"))
DATA_DIR = Path(os.environ.get("HESIA_UI_DATA_DIR", "/var/log/hesia/ui"))
LOG_FILE = Path(os.environ.get("HESIA_LOG_FILE", "/var/log/hesia/HESIA-SERVER-CPP.log"))
TREE_ROOT = Path(os.environ.get("HESIA_UI_TREE_ROOT", "/var/log/hesia"))
PORT = int(os.environ.get("HESIA_UI_PORT", "8080"))


def tail_lines(path: Path, n: int = 200, max_bytes: int = 1_000_000):
    if not path.exists():
        return []
    try:
        with path.open("rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(0, size - max_bytes), os.SEEK_SET)
            data = f.read()
        text = data.decode("utf-8", errors="replace")
        lines = text.splitlines()
        return lines[-n:]
    except Exception:
        return []


def safe_join(root: Path, user_path: str) -> Optional[Path]:
    try:
        rel = user_path.lstrip("/")
        target = (root / rel).resolve()
        if str(target).startswith(str(root.resolve())):
            return target
    except Exception:
        return None
    return None


class UIHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(UI_DIR), **kwargs)

    def _send_json(self, obj, code=200):
        payload = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/telemetry":
            telemetry_path = DATA_DIR / "telemetry.json"
            if telemetry_path.exists():
                try:
                    data = json.loads(telemetry_path.read_text(encoding="utf-8"))
                except Exception:
                    data = {}
            else:
                data = {}
            self._send_json(data)
            return

        if parsed.path == "/api/frame":
            frame_path = DATA_DIR / "latest.jpg"
            if not frame_path.exists():
                self.send_error(404, "No frame")
                return
            try:
                data = frame_path.read_bytes()
            except Exception:
                self.send_error(500, "Cannot read frame")
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        if parsed.path == "/api/frame_meta":
            meta_path = DATA_DIR / "frame_meta.json"
            if meta_path.exists():
                try:
                    data = json.loads(meta_path.read_text(encoding="utf-8"))
                except Exception:
                    data = {}
            else:
                data = {}
            self._send_json(data)
            return

        if parsed.path == "/api/logs":
            qs = parse_qs(parsed.query)
            lines = int(qs.get("lines", ["200"])[0])
            data = {
                "ts": int(time.time() * 1000),
                "lines": tail_lines(LOG_FILE, n=lines),
            }
            self._send_json(data)
            return

        if parsed.path == "/api/tree":
            qs = parse_qs(parsed.query)
            raw_path = qs.get("path", [""])[0]
            target = safe_join(TREE_ROOT, raw_path)
            if not target:
                self._send_json({"error": "invalid path"}, code=400)
                return
            if not target.exists():
                self._send_json({"error": "not found"}, code=404)
                return
            if target.is_file():
                target = target.parent
            items = []
            try:
                for entry in sorted(target.iterdir(), key=lambda p: (p.is_file(), p.name.lower())):
                    st = entry.stat()
                    item = {
                        "name": entry.name,
                        "type": "dir" if entry.is_dir() else "file",
                        "path": str(entry.resolve().relative_to(TREE_ROOT.resolve())),
                    }
                    if entry.is_file():
                        item["meta"] = f"{st.st_size} B"
                    items.append(item)
            except Exception:
                pass

            rel = str(target.resolve().relative_to(TREE_ROOT.resolve())) if TREE_ROOT.exists() else str(target)
            parent = None
            if target.resolve() != TREE_ROOT.resolve():
                parent = str(target.parent.resolve().relative_to(TREE_ROOT.resolve()))
            self._send_json({
                "path": "/" + rel,
                "parent": parent,
                "items": items,
            })
            return

        super().do_GET()


if __name__ == "__main__":
    UI_DIR.mkdir(parents=True, exist_ok=True)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", PORT), UIHandler)
    print(f"[UI] Serving {UI_DIR} on http://0.0.0.0:{PORT}")
    print(f"[UI] Data dir: {DATA_DIR}")
    print(f"[UI] Log file: {LOG_FILE}")
    print(f"[UI] Tree root: {TREE_ROOT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
