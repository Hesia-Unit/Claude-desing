#!/usr/bin/env python3
import hmac
import json
import os
import ssl
import threading
import time
from collections import deque
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional
from urllib.parse import parse_qs, urlparse

BASE_DIR = Path(__file__).resolve().parent.parent
UI_DIR = Path(os.environ.get("HESIA_UI_STATIC", BASE_DIR / "ui"))
DATA_DIR = Path(os.environ.get("HESIA_UI_DATA_DIR", "/var/log/hesia/ui"))
LOG_FILE = Path(os.environ.get("HESIA_LOG_FILE", "/var/log/hesia/HESIA-SERVER-CPP.log"))
BIND_ADDR = os.environ.get("HESIA_UI_BIND_ADDR", "127.0.0.1")
ALLOW_REMOTE = os.environ.get("HESIA_UI_ALLOW_REMOTE", "0") == "1"
TREE_ROOT = Path(os.environ.get("HESIA_UI_TREE_ROOT", str(DATA_DIR)))
PORT = int(os.environ.get("HESIA_UI_PORT", "8080"))
API_TOKEN = os.environ.get("HESIA_UI_API_TOKEN", "").strip()
ALLOW_INSECURE_LOCAL = os.environ.get("HESIA_UI_ALLOW_INSECURE_LOCAL", "0") == "1"
RATE_LIMIT_WINDOW_SEC = int(os.environ.get("HESIA_UI_RATE_LIMIT_WINDOW_SEC", "60"))
RATE_LIMIT_MAX_REQUESTS = int(os.environ.get("HESIA_UI_RATE_LIMIT_MAX_REQUESTS", "240"))
TLS_CERT = os.environ.get("HESIA_UI_TLS_CERT", "").strip()
TLS_KEY = os.environ.get("HESIA_UI_TLS_KEY", "").strip()
REQUIRE_TLS = os.environ.get("HESIA_UI_REQUIRE_TLS", "0") == "1"
_RATE_LIMIT = {}
_RATE_LIMIT_LOCK = threading.Lock()


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
        resolved_root = root.resolve()
        rel = user_path.lstrip("/")
        target = (resolved_root / rel).resolve()
        target.relative_to(resolved_root)
        return target
    except Exception:
        return None


def is_loopback_bind(host: str) -> bool:
    normalized = host.strip().lower()
    return normalized in {"127.0.0.1", "::1", "localhost"}


def is_loopback_client(host: str) -> bool:
    normalized = (host or "").strip().lower()
    return normalized in {"127.0.0.1", "::1", "::ffff:127.0.0.1", "localhost"}


class UIHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(UI_DIR), **kwargs)

    def end_headers(self):
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Cache-Control", "no-store")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
            "script-src 'self'; frame-ancestors 'none'; base-uri 'self'",
        )
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        if isinstance(self.connection, ssl.SSLSocket):
            self.send_header("Strict-Transport-Security", "max-age=63072000; includeSubDomains")
        super().end_headers()

    def _send_json(self, obj, code=200):
        payload = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _check_rate_limit(self) -> bool:
        client_ip = self.client_address[0] if self.client_address else "unknown"
        limit = RATE_LIMIT_MAX_REQUESTS * 4 if is_loopback_client(client_ip) else RATE_LIMIT_MAX_REQUESTS
        now = time.monotonic()
        cutoff = now - RATE_LIMIT_WINDOW_SEC
        with _RATE_LIMIT_LOCK:
            bucket = _RATE_LIMIT.setdefault(client_ip, deque())
            while bucket and bucket[0] < cutoff:
                bucket.popleft()
            if len(bucket) >= limit:
                return False
            bucket.append(now)
        return True

    def _authorize_api(self) -> bool:
        if not self._check_rate_limit():
            self._send_json({"error": "rate limit exceeded"}, code=429)
            return False
        if not API_TOKEN:
            return True
        provided = self.headers.get("Authorization", "")
        expected = f"Bearer {API_TOKEN}"
        if not hmac.compare_digest(provided, expected):
            self._send_json({"error": "unauthorized"}, code=401)
            return False
        return True

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/") and not self._authorize_api():
            return

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
            try:
                lines = int(qs.get("lines", ["200"])[0])
            except Exception:
                lines = 200
            lines = max(1, min(lines, 1000))
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


def tls_enabled() -> bool:
    return bool(TLS_CERT and TLS_KEY)


def build_ssl_context() -> ssl.SSLContext:
    if not tls_enabled():
        raise RuntimeError("TLS certificate/key not configured")
    cert_path = Path(TLS_CERT)
    key_path = Path(TLS_KEY)
    if not cert_path.is_file():
        raise SystemExit(f"Configured UI TLS certificate not found: {cert_path}")
    if not key_path.is_file():
        raise SystemExit(f"Configured UI TLS key not found: {key_path}")
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    ctx.options |= ssl.OP_NO_COMPRESSION
    ctx.load_cert_chain(certfile=str(cert_path), keyfile=str(key_path))
    return ctx


if __name__ == "__main__":
    UI_DIR.mkdir(parents=True, exist_ok=True)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    try:
        os.chmod(DATA_DIR, 0o700)
    except OSError:
        pass
    if not is_loopback_bind(BIND_ADDR) and not ALLOW_REMOTE:
        raise SystemExit("Refusing remote UI bind without HESIA_UI_ALLOW_REMOTE=1")
    if not API_TOKEN and not ALLOW_INSECURE_LOCAL:
        raise SystemExit("Refusing UI start without HESIA_UI_API_TOKEN (set HESIA_UI_ALLOW_INSECURE_LOCAL=1 only for throwaway local debug)")
    if (ALLOW_REMOTE or not is_loopback_bind(BIND_ADDR)) and not API_TOKEN:
        raise SystemExit("Refusing remote UI bind without HESIA_UI_API_TOKEN")
    if ((ALLOW_REMOTE or not is_loopback_bind(BIND_ADDR)) or REQUIRE_TLS) and not tls_enabled():
        raise SystemExit("Refusing UI start without HESIA_UI_TLS_CERT and HESIA_UI_TLS_KEY")
    server = ThreadingHTTPServer((BIND_ADDR, PORT), UIHandler)
    if tls_enabled():
        server.socket = build_ssl_context().wrap_socket(server.socket, server_side=True)
    scheme = "https" if tls_enabled() else "http"
    print(f"[UI] Serving {UI_DIR} on {scheme}://{BIND_ADDR}:{PORT}")
    print(f"[UI] Data dir: {DATA_DIR}")
    print(f"[UI] Log file: {LOG_FILE}")
    print(f"[UI] Tree root: {TREE_ROOT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
