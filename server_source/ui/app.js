const gauges = Array.from(document.querySelectorAll('.gauge'));
const logView = document.getElementById('logView');
const videoFrame = document.getElementById('videoFrame');
const videoStatus = document.getElementById('videoStatus');
const frameMeta = document.getElementById('frameMeta');
const gpsReadout = document.getElementById('gpsReadout');
const jetsonId = document.getElementById('jetsonId');
const fileTree = document.getElementById('fileTree');
const filePath = document.getElementById('filePath');

let telemetry = {};
let frameHistory = [];
let frameIndex = -1;
let live = true;
let paused = false;
const maxHistory = 120;
let framePending = false;
let pendingUrl = '';
let frameFailures = 0;

function setGauge(gauge, value, max, unit) {
  const needle = gauge.querySelector('.gauge-needle');
  const valueEl = gauge.querySelector('.gauge-value');
  const safeVal = Number.isFinite(value) ? value : -1;
  const safeMax = Number.isFinite(max) && max > 0 ? max : 1;
  const ratio = Math.max(0, Math.min(1, safeVal / safeMax));
  const deg = -130 + ratio * 260;
  needle.style.transform = `translateX(-50%) rotate(${deg}deg)`;
  valueEl.textContent = safeVal >= 0 ? `${safeVal.toFixed(2)} ${unit}` : '--';
  if (safeVal >= safeMax * 0.9) {
    needle.style.background = 'var(--err)';
  } else if (safeVal >= safeMax * 0.75) {
    needle.style.background = 'var(--warn)';
  } else {
    needle.style.background = 'var(--accent)';
  }
}

async function fetchTelemetry() {
  try {
    const res = await fetch(`/api/telemetry?ts=${Date.now()}`);
    if (!res.ok) return;
    telemetry = await res.json();
    gauges.forEach(g => {
      const key = g.dataset.key;
      const unit = g.dataset.unit || '';
      let max = parseFloat(g.dataset.max || '1');
      if (g.dataset.total) {
        const totalKey = g.dataset.total;
        const totalVal = telemetry[totalKey];
        if (Number.isFinite(totalVal) && totalVal > 0) max = totalVal;
      }
      setGauge(g, telemetry[key], max, unit);
    });
    const lat = telemetry.gps_lat ?? 0;
    const lon = telemetry.gps_lon ?? 0;
    gpsReadout.textContent = `GPS: ${lat.toFixed(5)}, ${lon.toFixed(5)}`;
    jetsonId.textContent = `DRONE: ${telemetry.drone_id || '--'}`;
  } catch (_) {
    // ignore
  }
}

function classifyLog(line) {
  const upper = line.toUpperCase();
  if (upper.includes('ERROR') || upper.includes('ERREUR')) return 'error';
  if (upper.includes('CONST') || upper.includes('TELEMETRY')) return 'const';
  if (upper.includes('WARN')) return 'info';
  if (upper.includes('INFO')) return 'info';
  return 'debug';
}

async function fetchLogs() {
  try {
    const res = await fetch(`/api/logs?lines=200&ts=${Date.now()}`);
    if (!res.ok) return;
    const data = await res.json();
    const lines = data.lines || [];
    logView.innerHTML = lines.map(l => {
      const cls = classifyLog(l);
      return `<div class="log-line ${cls}">${l}</div>`;
    }).join('');
    logView.scrollTop = logView.scrollHeight;
  } catch (_) {
    // ignore
  }
}

async function fetchFrameMeta() {
  try {
    const res = await fetch(`/api/frame_meta?ts=${Date.now()}`);
    if (!res.ok) return;
    const data = await res.json();
    if (data.frame_id !== undefined) {
      frameMeta.textContent = `FRAME ${data.frame_id}`;
    }
  } catch (_) {
    // ignore
  }
}

async function fetchFrame() {
  if (!live || paused || framePending) return;
  try {
    pendingUrl = `/api/frame?ts=${Date.now()}`;
    framePending = true;
    videoFrame.src = pendingUrl;
  } catch (_) {
    framePending = false;
  }
}

function showFrame(index) {
  if (index < 0 || index >= frameHistory.length) return;
  frameIndex = index;
  videoFrame.src = frameHistory[frameIndex];
}

document.getElementById('btnBack').addEventListener('click', () => {
  if (frameHistory.length === 0) return;
  live = false;
  paused = true;
  videoStatus.textContent = 'PAUSED';
  showFrame(Math.max(0, frameIndex - 1));
});

document.getElementById('btnPause').addEventListener('click', () => {
  paused = !paused;
  videoStatus.textContent = paused ? 'PAUSED' : (live ? 'LIVE' : 'HOLD');
});

document.getElementById('btnLive').addEventListener('click', () => {
  live = true;
  paused = false;
  videoStatus.textContent = 'LIVE';
  if (frameHistory.length > 0) {
    showFrame(frameHistory.length - 1);
  }
});

videoFrame.addEventListener('load', () => {
  if (!framePending || !pendingUrl) return;
  framePending = false;
  frameFailures = 0;
  videoStatus.textContent = live && !paused ? 'LIVE' : (paused ? 'PAUSED' : 'HOLD');
  frameHistory.push(pendingUrl);
  if (frameHistory.length > maxHistory) {
    frameHistory.shift();
  }
  frameIndex = frameHistory.length - 1;
  pendingUrl = '';
});

videoFrame.addEventListener('error', () => {
  if (!framePending) return;
  framePending = false;
  pendingUrl = '';
  frameFailures += 1;
  if (frameFailures >= 3) {
    videoStatus.textContent = 'NO FEED';
  }
});

async function fetchTree(path = '') {
  try {
    const res = await fetch(`/api/tree?path=${encodeURIComponent(path)}`);
    if (!res.ok) return;
    const data = await res.json();
    filePath.textContent = data.path || '/';
    fileTree.innerHTML = '';
    if (data.parent) {
      const up = document.createElement('div');
      up.className = 'file-item dir';
      up.innerHTML = `<div class="name">..</div><div class="meta">parent</div>`;
      up.addEventListener('click', () => fetchTree(data.parent));
      fileTree.appendChild(up);
    }
    (data.items || []).forEach(item => {
      const div = document.createElement('div');
      div.className = `file-item ${item.type}`;
      div.innerHTML = `<div class="name">${item.name}</div><div class="meta">${item.meta || ''}</div>`;
      if (item.type === 'dir') {
        div.addEventListener('click', () => fetchTree(item.path));
      }
      fileTree.appendChild(div);
    });
  } catch (_) {
    // ignore
  }
}

// Globe rendering
const globeCanvas = document.getElementById('globeCanvas');
const gctx = globeCanvas.getContext('2d');
let rot = 0;

function projectPoint(lat, lon, rot) {
  const phi = (lat * Math.PI) / 180;
  const theta = (lon * Math.PI) / 180 + rot;
  const x = Math.cos(phi) * Math.cos(theta);
  const y = Math.sin(phi);
  const z = Math.cos(phi) * Math.sin(theta);
  return { x, y, z };
}

function drawGlobe() {
  const w = globeCanvas.width;
  const h = globeCanvas.height;
  const cx = w / 2;
  const cy = h / 2;
  const r = Math.min(w, h) * 0.38;
  gctx.clearRect(0, 0, w, h);

  // outer glow
  gctx.beginPath();
  gctx.arc(cx, cy, r + 8, 0, Math.PI * 2);
  gctx.strokeStyle = 'rgba(0,255,224,0.12)';
  gctx.stroke();

  // grid
  gctx.strokeStyle = 'rgba(0,255,224,0.25)';
  gctx.lineWidth = 1;
  for (let lat = -60; lat <= 60; lat += 30) {
    gctx.beginPath();
    for (let lon = 0; lon <= 360; lon += 8) {
      const p = projectPoint(lat, lon, rot);
      if (p.z < 0) continue;
      const x = cx + p.x * r;
      const y = cy + p.y * r;
      if (lon === 0) gctx.moveTo(x, y); else gctx.lineTo(x, y);
    }
    gctx.stroke();
  }
  for (let lon = 0; lon < 180; lon += 30) {
    gctx.beginPath();
    for (let lat = -90; lat <= 90; lat += 5) {
      const p = projectPoint(lat, lon, rot);
      if (p.z < 0) continue;
      const x = cx + p.x * r;
      const y = cy + p.y * r;
      if (lat === -90) gctx.moveTo(x, y); else gctx.lineTo(x, y);
    }
    gctx.stroke();
  }

  // drone dot
  const lat = telemetry.gps_lat ?? 0;
  const lon = telemetry.gps_lon ?? 0;
  const p = projectPoint(lat, lon, rot);
  if (p.z > 0) {
    const x = cx + p.x * r;
    const y = cy + p.y * r;
    gctx.beginPath();
    gctx.arc(x, y, 4, 0, Math.PI * 2);
    gctx.fillStyle = 'rgba(255,255,255,0.9)';
    gctx.fill();
    gctx.beginPath();
    gctx.arc(x, y, 10, 0, Math.PI * 2);
    gctx.strokeStyle = 'rgba(0,255,224,0.7)';
    gctx.stroke();
  }

  rot += 0.003;
  requestAnimationFrame(drawGlobe);
}

// Timers
setInterval(fetchTelemetry, 500);
setInterval(fetchLogs, 1000);
setInterval(fetchFrameMeta, 1000);
setInterval(fetchFrame, 33);
setInterval(() => fetchTree(''), 5000);

drawGlobe();
fetchTelemetry();
fetchLogs();
fetchTree('');
