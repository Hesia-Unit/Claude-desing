#!/usr/bin/env python3
"""Local HESIA Jetson-Ajax test console."""

from __future__ import annotations

import argparse
import json
import mimetypes
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


REPO_ROOT = Path(__file__).resolve().parents[2]
JETSON_ARTIFACTS = REPO_ROOT / "artifacts" / "jetson_ajax"
ALPHA_ARTIFACTS = REPO_ROOT / "artifacts" / "alpha"
F_WORKSPACE = Path("F:/Jetson-Ajax")


PHASES = [
    (0, "Inventory", "Image, tools, Docker/WSL, constraints", "PHASE_0_INVENTORY_REPORT"),
    (1, "Workspace", "F: layout, source image protection, manifest", "PHASE_1_WORKSPACE_REPORT"),
    (2, "Rootfs Mount", "Read-only Jetson APP/ESP mount and manifest", "PHASE_2_ROOTFS_MOUNT_REPORT"),
    (3, "Docker Jetson", "Rootfs visible in simulation container", "PHASE_3_DOCKER_SIM_REPORT"),
    (4, "Hardware Mirror", "Desktop CPU/GPU values mirrored into sim paths", "PHASE_4_HARDWARE_MIRROR_REPORT"),
    (5, "PLFM Radar", "Radar replay bridged into HESIA JSON schema", "PHASE_5_PLFM_RADAR_BRIDGE_REPORT"),
    (6, "I/O Security", "Bounds checks and fuzz probes", "PHASE_6_IO_SECURITY_REPORT"),
    (7, "Server mTLS", "HESIA server build, policy and mTLS proof", "PHASE_7_SERVER_MTLS_REPORT"),
    (8, "Pixhawk SITL", "ArduCopter SITL and MAVLink bridge", "PHASE_8_PIXHAWK_SITL_REPORT"),
    (9, "Mission Loop", "Camera segmentation, telemetry fusion and decision", "PHASE_9_MISSION_LOOP_REPORT"),
    (10, "Test GUI", "Local graphical console for test phases", "PHASE_10_TEST_GUI_REPORT"),
    (11, "System Tests", "End-to-end Docker/WSL validation", "PHASE_11_SYSTEM_TESTS_REPORT"),
    (12, "Autonomy AI", "YOLO11m-seg perception and Mamba-2 decision stack", "PHASE_12_AUTONOMY_AI_REPORT"),
    (13, "Final PDF", "Complete recap PDF", "HESIA_JETSON_AJAX_FINAL_REPORT"),
]


LOG_FILES = {
    "phase7-server": JETSON_ARTIFACTS / "phase7_server_after_handshake.log",
    "phase8-pixhawk": JETSON_ARTIFACTS / "phase8_docker_pixhawk_logs.txt",
    "phase8-build": JETSON_ARTIFACTS / "phase8_arducopter_build.log",
    "phase9-summary": JETSON_ARTIFACTS / "phase9_mission_loop_summary.pretty.json",
    "pixhawk-jsonl": F_WORKSPACE / "artifacts" / "tests" / "pixhawk_mavlink_state_rerun.jsonl",
    "radar-jsonl": F_WORKSPACE / "plfm_radar" / "bridge_logs" / "small_test_hesia_radar.jsonl",
}


def file_info(path: Path) -> dict[str, Any]:
    exists = path.exists()
    stat = path.stat() if exists else None
    return {
        "path": str(path),
        "exists": exists,
        "bytes": stat.st_size if stat else 0,
        "mtime": stat.st_mtime if stat else None,
    }


def read_json(path: Path) -> Any:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def phase_status() -> list[dict[str, Any]]:
    items = []
    for idx, name, description, stem in PHASES:
        report_md = JETSON_ARTIFACTS / f"{stem}.md"
        report_pdf = JETSON_ARTIFACTS / f"{stem}.pdf"
        if idx == 13:
            report_md = JETSON_ARTIFACTS / f"{stem}.md"
            report_pdf = JETSON_ARTIFACTS / f"{stem}.pdf"
        status = "completed" if report_md.exists() and report_pdf.exists() else "pending"
        items.append(
            {
                "id": idx,
                "name": name,
                "description": description,
                "status": status,
                "report_md": file_info(report_md),
                "report_pdf": file_info(report_pdf),
            }
        )
    return items


def status_payload() -> dict[str, Any]:
    mission = read_json(JETSON_ARTIFACTS / "phase9_mission_loop_summary.json")
    pixhawk = read_json(F_WORKSPACE / "artifacts" / "tests" / "pixhawk_mavlink_summary_rerun.json")
    radar = None
    radar_path = F_WORKSPACE / "plfm_radar" / "bridge_logs" / "small_test_hesia_radar.jsonl"
    if radar_path.exists():
        for line in radar_path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                radar = json.loads(line)
    docker_images = (JETSON_ARTIFACTS / "phase8_docker_image_digests.txt").read_text(encoding="utf-8") if (JETSON_ARTIFACTS / "phase8_docker_image_digests.txt").exists() else ""
    return {
        "schema": "hesia.jetson_ajax.gui.status.v1",
        "repo_root": str(REPO_ROOT),
        "f_workspace": str(F_WORKSPACE),
        "phases": phase_status(),
        "mission": mission,
        "pixhawk": pixhawk,
        "radar": radar,
        "docker_images": docker_images.strip().splitlines(),
        "overlay": file_info(JETSON_ARTIFACTS / "phase9_mission_loop_overlay.png"),
        "logs": {key: file_info(path) for key, path in LOG_FILES.items()},
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "HesiaJetsonAjaxGUI/1.0"

    def _send(self, code: int, body: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, payload: Any, code: int = 200) -> None:
        self._send(code, json.dumps(payload, indent=2, sort_keys=True).encode("utf-8"), "application/json; charset=utf-8")

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self._send(200, INDEX_HTML.encode("utf-8"), "text/html; charset=utf-8")
            return
        if parsed.path == "/app.css":
            self._send(200, APP_CSS.encode("utf-8"), "text/css; charset=utf-8")
            return
        if parsed.path == "/app.js":
            self._send(200, APP_JS.encode("utf-8"), "application/javascript; charset=utf-8")
            return
        if parsed.path == "/api/status":
            self._json(status_payload())
            return
        if parsed.path == "/api/log":
            name = parse_qs(parsed.query).get("name", [""])[0]
            path = LOG_FILES.get(name)
            if path is None:
                self._json({"error": "unknown log name"}, 404)
                return
            if not path.exists():
                self._json({"name": name, "path": str(path), "exists": False, "text": ""})
                return
            text = path.read_text(encoding="utf-8", errors="replace")
            lines = text.splitlines()[-260:]
            self._json({"name": name, "path": str(path), "exists": True, "text": "\n".join(lines)})
            return
        if parsed.path == "/api/overlay":
            path = JETSON_ARTIFACTS / "phase9_mission_loop_overlay.png"
            if not path.exists():
                self._json({"error": "overlay missing"}, 404)
                return
            self._send(200, path.read_bytes(), mimetypes.guess_type(path.name)[0] or "image/png")
            return
        self._json({"error": "not found"}, 404)

    def log_message(self, format: str, *args: Any) -> None:
        return


INDEX_HTML = """<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>HESIA Jetson-Ajax Test Console</title>
  <link rel="stylesheet" href="/app.css">
</head>
<body>
  <div class="shell">
    <header class="topbar">
      <div>
        <p class="eyebrow">HESIA JETSON-AJAX</p>
        <h1>Console de banc d'essai</h1>
      </div>
      <div class="toolbar">
        <button id="refreshBtn" type="button">Refresh</button>
        <span id="updatedAt" class="clock">--</span>
      </div>
    </header>

    <section class="status-strip" id="statusStrip"></section>

    <main class="layout">
      <section class="phase-panel">
        <div class="panel-title">
          <h2>Phases</h2>
          <span id="phaseCount">--</span>
        </div>
        <div id="phaseList" class="phase-list"></div>
      </section>

      <section class="work-panel">
        <div class="tabs">
          <button class="tab active" data-tab="mission" type="button">Mission</button>
          <button class="tab" data-tab="pixhawk" type="button">Pixhawk</button>
          <button class="tab" data-tab="proofs" type="button">Preuves</button>
        </div>
        <div id="missionTab" class="tab-body active">
          <div class="mission-grid">
            <div class="mission-summary">
              <h2>Decision IA</h2>
              <dl id="decisionGrid"></dl>
            </div>
            <div class="overlay-box">
              <img id="overlayImage" alt="Segmentation mission">
            </div>
          </div>
          <div class="table-wrap">
            <table>
              <thead><tr><th>Classe</th><th>Fraction</th></tr></thead>
              <tbody id="classRows"></tbody>
            </table>
          </div>
        </div>
        <div id="pixhawkTab" class="tab-body">
          <div class="split">
            <pre id="pixhawkText"></pre>
            <pre id="radarText"></pre>
          </div>
        </div>
        <div id="proofsTab" class="tab-body">
          <div class="log-buttons">
            <button data-log="phase7-server" type="button">Server mTLS</button>
            <button data-log="phase8-pixhawk" type="button">Pixhawk logs</button>
            <button data-log="phase9-summary" type="button">Mission JSON</button>
            <button data-log="radar-jsonl" type="button">Radar JSONL</button>
          </div>
          <pre id="logText"></pre>
        </div>
      </section>
    </main>
  </div>
  <script src="/app.js"></script>
</body>
</html>
"""


APP_CSS = """
:root {
  color-scheme: light;
  --bg: #eef2f4;
  --panel: #ffffff;
  --ink: #132027;
  --muted: #5d6a72;
  --line: #ccd6db;
  --ok: #138a5b;
  --warn: #a06900;
  --pending: #6a737b;
  --accent: #206b7a;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  min-height: 100vh;
  background: var(--bg);
  color: var(--ink);
  font: 14px/1.4 Arial, Helvetica, sans-serif;
}
button {
  border: 1px solid var(--line);
  background: #f7fafb;
  color: var(--ink);
  border-radius: 6px;
  min-height: 34px;
  padding: 0 12px;
  cursor: pointer;
}
button:hover { border-color: var(--accent); }
.shell { width: min(1440px, 100%); margin: 0 auto; padding: 18px; }
.topbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
  margin-bottom: 14px;
}
.eyebrow {
  margin: 0 0 4px;
  color: var(--accent);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0;
}
h1, h2 { margin: 0; letter-spacing: 0; }
h1 { font-size: 24px; }
h2 { font-size: 16px; }
.toolbar { display: flex; align-items: center; gap: 10px; }
.clock { color: var(--muted); min-width: 96px; text-align: right; }
.status-strip {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 10px;
  margin-bottom: 14px;
}
.stat {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 12px;
  min-height: 78px;
}
.stat span { color: var(--muted); font-size: 12px; }
.stat strong { display: block; margin-top: 6px; font-size: 20px; }
.layout {
  display: grid;
  grid-template-columns: 360px minmax(0, 1fr);
  gap: 14px;
}
.phase-panel, .work-panel {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
  min-height: 680px;
}
.panel-title {
  display: flex;
  justify-content: space-between;
  padding: 14px;
  border-bottom: 1px solid var(--line);
}
.phase-list { max-height: 626px; overflow: auto; }
.phase-item {
  display: grid;
  grid-template-columns: 36px minmax(0, 1fr) 88px;
  gap: 10px;
  align-items: center;
  border-bottom: 1px solid var(--line);
  padding: 10px 14px;
  cursor: pointer;
}
.phase-item:hover, .phase-item.active { background: #edf7f8; }
.phase-id {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  background: #e3eaee;
  font-weight: 700;
}
.phase-name { font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.phase-desc { color: var(--muted); font-size: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.badge {
  border-radius: 999px;
  padding: 4px 8px;
  font-size: 12px;
  text-align: center;
  border: 1px solid var(--line);
}
.completed { color: var(--ok); background: #e8f5ef; }
.pending { color: var(--pending); background: #f1f3f4; }
.tabs {
  display: flex;
  gap: 8px;
  padding: 12px;
  border-bottom: 1px solid var(--line);
}
.tab.active { background: var(--accent); color: #fff; border-color: var(--accent); }
.tab-body { display: none; padding: 14px; }
.tab-body.active { display: block; }
.mission-grid {
  display: grid;
  grid-template-columns: 360px minmax(0, 1fr);
  gap: 14px;
  align-items: start;
}
.mission-summary, .overlay-box, .table-wrap {
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 12px;
  background: #fbfcfd;
}
dl {
  display: grid;
  grid-template-columns: 150px minmax(0, 1fr);
  gap: 8px 10px;
  margin: 12px 0 0;
}
dt { color: var(--muted); }
dd { margin: 0; font-weight: 700; overflow-wrap: anywhere; }
.overlay-box img {
  display: block;
  width: 100%;
  max-height: 420px;
  object-fit: contain;
  background: #101820;
  border-radius: 6px;
}
.table-wrap { margin-top: 14px; overflow: auto; }
table { width: 100%; border-collapse: collapse; }
th, td { text-align: left; padding: 8px; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-size: 12px; }
.split {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 14px;
}
pre {
  margin: 0;
  padding: 12px;
  min-height: 520px;
  max-height: 640px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-word;
  background: #11191f;
  color: #d8e9ef;
  border-radius: 8px;
}
.log-buttons { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 12px; }
@media (max-width: 980px) {
  .status-strip, .layout, .mission-grid, .split { grid-template-columns: 1fr; }
  .phase-panel, .work-panel { min-height: auto; }
}
"""


APP_JS = """
let state = null;
let activePhase = 0;

const fmt = (value, digits = 2) => Number.isFinite(value) ? value.toFixed(digits) : '--';
const pct = (value) => Number.isFinite(value) ? `${(value * 100).toFixed(1)}%` : '--';

async function loadStatus() {
  const res = await fetch('/api/status', { cache: 'no-store' });
  state = await res.json();
  render();
}

function renderStats() {
  const mission = state.mission || {};
  const pix = state.pixhawk || {};
  const phases = state.phases || [];
  const completed = phases.filter(p => p.status === 'completed').length;
  const decision = mission.decision || {};
  const stats = [
    ['Phases validees', `${completed} / ${phases.length}`],
    ['Pixhawk snapshots', pix.snapshot_count || '--'],
    ['Decision', decision.mode || '--'],
    ['Vision latency', `${fmt((mission.latency_ms || {}).alpha_segmentation_forward, 2)} ms`],
  ];
  document.getElementById('statusStrip').innerHTML = stats.map(([label, value]) => (
    `<article class="stat"><span>${label}</span><strong>${value}</strong></article>`
  )).join('');
}

function renderPhases() {
  const phases = state.phases || [];
  document.getElementById('phaseCount').textContent = `${phases.length} points`;
  document.getElementById('phaseList').innerHTML = phases.map(phase => `
    <div class="phase-item ${phase.id === activePhase ? 'active' : ''}" data-phase="${phase.id}">
      <div class="phase-id">${phase.id}</div>
      <div>
        <div class="phase-name">${phase.name}</div>
        <div class="phase-desc">${phase.description}</div>
      </div>
      <div class="badge ${phase.status}">${phase.status}</div>
    </div>
  `).join('');
  document.querySelectorAll('.phase-item').forEach(item => {
    item.addEventListener('click', () => {
      activePhase = Number(item.dataset.phase);
      renderPhases();
      renderProofs();
    });
  });
}

function renderMission() {
  const mission = state.mission || {};
  const decision = mission.decision || {};
  const nav = decision.navigation_error || {};
  const risk = decision.perception_risk || {};
  const cmd = decision.command || {};
  const rows = [
    ['Mode', decision.mode],
    ['Distance', `${fmt(nav.distance_m, 1)} m`],
    ['Bearing', `${fmt(nav.bearing_deg, 1)} deg`],
    ['Altitude error', `${fmt(nav.altitude_error_m, 2)} m`],
    ['Obstacle', pct(risk.obstacle_fraction)],
    ['Critical', pct(risk.critical_fraction)],
    ['Command N/E/Z', `${fmt(cmd.north_mps)} / ${fmt(cmd.east_mps)} / ${fmt(cmd.climb_mps)} mps`],
    ['Autopilot send', String(cmd.send_to_autopilot)],
  ];
  document.getElementById('decisionGrid').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v ?? '--'}</dd>`).join('');
  const classes = ((mission.segmentation || {}).top_classes || []);
  document.getElementById('classRows').innerHTML = classes.map(item => `<tr><td>${item.name}</td><td>${pct(item.fraction)}</td></tr>`).join('');
  document.getElementById('overlayImage').src = `/api/overlay?ts=${Date.now()}`;
}

function renderPixhawk() {
  const mission = state.mission || {};
  document.getElementById('pixhawkText').textContent = JSON.stringify(mission.pixhawk_state || state.pixhawk || {}, null, 2);
  document.getElementById('radarText').textContent = JSON.stringify({ health: mission.radar_health, radar: state.radar }, null, 2);
}

async function loadLog(name) {
  const res = await fetch(`/api/log?name=${encodeURIComponent(name)}`, { cache: 'no-store' });
  const data = await res.json();
  document.getElementById('logText').textContent = data.text || JSON.stringify(data, null, 2);
}

function renderProofs() {
  const phase = (state.phases || []).find(p => p.id === activePhase);
  if (phase) {
    document.getElementById('logText').textContent = JSON.stringify({
      phase: phase.name,
      status: phase.status,
      report_md: phase.report_md,
      report_pdf: phase.report_pdf,
    }, null, 2);
  }
}

function bindTabs() {
  document.querySelectorAll('.tab').forEach(button => {
    button.addEventListener('click', () => {
      document.querySelectorAll('.tab').forEach(item => item.classList.remove('active'));
      document.querySelectorAll('.tab-body').forEach(item => item.classList.remove('active'));
      button.classList.add('active');
      document.getElementById(`${button.dataset.tab}Tab`).classList.add('active');
    });
  });
  document.querySelectorAll('[data-log]').forEach(button => {
    button.addEventListener('click', () => loadLog(button.dataset.log));
  });
}

function render() {
  renderStats();
  renderPhases();
  renderMission();
  renderPixhawk();
  renderProofs();
  document.getElementById('updatedAt').textContent = new Date().toLocaleTimeString();
}

document.getElementById('refreshBtn').addEventListener('click', loadStatus);
bindTabs();
loadStatus();
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"HESIA Jetson-Ajax GUI listening on http://{args.host}:{args.port}")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
