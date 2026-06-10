const logView = document.getElementById('logView');
const videoFrame = document.getElementById('videoFrame');
const videoStatus = document.getElementById('videoStatus');
const sessionStatus = document.getElementById('sessionStatus');
const feedFreshness = document.getElementById('feedFreshness');
const overlayStatus = document.getElementById('overlayStatus');
const frameMeta = document.getElementById('frameMeta');
const frameBytes = document.getElementById('frameBytes');
const videoFallback = document.getElementById('videoFallback');
const videoFallbackReason = document.getElementById('videoFallbackReason');
const gpsReadout = document.getElementById('gpsReadout');
const jetsonId = document.getElementById('jetsonId');
const fileTree = document.getElementById('fileTree');
const filePath = document.getElementById('filePath');

const metricCpuTemp = document.getElementById('metricCpuTemp');
const metricCpuTempMeta = document.getElementById('metricCpuTempMeta');
const metricCpuUsage = document.getElementById('metricCpuUsage');
const metricCpuUsageMeta = document.getElementById('metricCpuUsageMeta');
const metricRam = document.getElementById('metricRam');
const metricRamMeta = document.getElementById('metricRamMeta');
const metricPower = document.getElementById('metricPower');
const metricPowerMeta = document.getElementById('metricPowerMeta');
const metricGps = document.getElementById('metricGps');
const metricGpsMeta = document.getElementById('metricGpsMeta');
const metricDrone = document.getElementById('metricDrone');
const metricDroneMeta = document.getElementById('metricDroneMeta');

let telemetry = {};
let frameHistory = [];
let frameIndex = -1;
let live = true;
let paused = false;
let currentTreePath = '';
let frameFetchInFlight = false;
let lastLoadedFrameId = null;
let lastSeenMetaFrameId = null;
let lastFrameMetaAt = 0;
let lastFrameLoadedAt = 0;
let lastFrameRequestAt = 0;
let frameErrorCount = 0;
let activeObjectUrl = null;

const maxHistory = 120;
const maxFrameFetchRateMs = 1000;

function isFiniteMetric(value) {
  return Number.isFinite(value) && value >= 0;
}

function metricText(value, digits = 1, unit = '') {
  if (!isFiniteMetric(value)) return '--';
  return `${value.toFixed(digits)}${unit}`;
}

function sinceText(msSince) {
  if (!Number.isFinite(msSince) || msSince < 0) return '--';
  if (msSince < 1000) return `${Math.round(msSince)} ms`;
  return `${(msSince / 1000).toFixed(1)} s`;
}

function classifyLog(line) {
  const upper = line.toUpperCase();
  if (upper.includes('ERROR') || upper.includes('ERREUR') || upper.includes('FAILED')) return 'error';
  if (upper.includes('WARN') || upper.includes('WARNING')) return 'warn';
  if (upper.includes('CONST') || upper.includes('TELEMETRY') || upper.includes('ESTABLISHED')) return 'const';
  return 'info';
}

function renderLogLines(lines) {
  const nearBottom = logView.scrollHeight - logView.scrollTop - logView.clientHeight < 80;
  const fragment = document.createDocumentFragment();
  lines.forEach(line => {
    const div = document.createElement('div');
    div.className = `log-line ${classifyLog(line)}`;
    div.textContent = line;
    fragment.appendChild(div);
  });
  logView.replaceChildren(fragment);
  if (nearBottom) {
    logView.scrollTop = logView.scrollHeight;
  }
}

function createFileItem(name, meta, type, onClick) {
  const div = document.createElement('div');
  div.className = `file-item ${type}`;

  const nameEl = document.createElement('div');
  nameEl.className = 'name';
  nameEl.textContent = name;
  div.appendChild(nameEl);

  const metaEl = document.createElement('div');
  metaEl.className = 'meta';
  metaEl.textContent = typeof meta === 'string' ? meta : '';
  div.appendChild(metaEl);

  if (onClick) {
    div.addEventListener('click', onClick);
  }
  return div;
}

function setSessionState(label) {
  sessionStatus.textContent = label;
}

function setVideoState(label, reason = '') {
  videoStatus.textContent = label;
  overlayStatus.textContent = label;
  if (label === 'LIVE') {
    videoFallback.hidden = true;
  } else {
    videoFallback.hidden = false;
    videoFallbackReason.textContent = reason || 'Le serveur attend la prochaine image decodee.';
  }
}

function updateFreshness() {
  const now = Date.now();
  if (!lastFrameLoadedAt) {
    feedFreshness.textContent = '--';
    setVideoState('SYNC', 'Le flux n a pas encore livre d image exploitable a l interface.');
    return;
  }

  const age = now - lastFrameLoadedAt;
  feedFreshness.textContent = sinceText(age);

  if (paused) {
    setVideoState('PAUSED', 'Le flux est fige a votre demande.');
    return;
  }
  if (!live) {
    setVideoState('HOLD', 'Historique local affiche.');
    return;
  }
  if (age > 7000) {
    setVideoState('NO FEED', 'Aucune nouvelle frame recue depuis plusieurs secondes.');
  } else if (age > 2500) {
    setVideoState('STALE', 'Le serveur reste connecte mais le flux se rafraichit lentement.');
  } else {
    setVideoState('LIVE');
  }
}

function updateMetricBlock(valueEl, metaEl, value, unit, meta) {
  valueEl.textContent = metricText(value, unit === '%' ? 0 : 2, unit);
  metaEl.textContent = meta;
}

function renderTelemetry() {
  updateMetricBlock(metricCpuTemp, metricCpuTempMeta, telemetry.cpu_temp_c, ' C', isFiniteMetric(telemetry.cpu_temp_c) ? 'temperature capteur' : 'valeur indisponible');
  updateMetricBlock(metricCpuUsage, metricCpuUsageMeta, telemetry.cpu_usage_pct, '%', isFiniteMetric(telemetry.cpu_usage_pct) ? 'charge instantanee' : 'valeur indisponible');

  if (isFiniteMetric(telemetry.ram_used_mb) && isFiniteMetric(telemetry.ram_total_mb) && telemetry.ram_total_mb > 0) {
    metricRam.textContent = `${telemetry.ram_used_mb.toFixed(0)} / ${telemetry.ram_total_mb.toFixed(0)} MB`;
    metricRamMeta.textContent = `${((telemetry.ram_used_mb / telemetry.ram_total_mb) * 100).toFixed(1)} % utilise`;
  } else {
    metricRam.textContent = '--';
    metricRamMeta.textContent = 'valeur indisponible';
  }

  if (isFiniteMetric(telemetry.power_w)) {
    metricPower.textContent = `${telemetry.power_w.toFixed(2)} W`;
    const volts = metricText(telemetry.voltage_v, 2, ' V');
    const amps = metricText(telemetry.current_a, 2, ' A');
    metricPowerMeta.textContent = `${volts} / ${amps}`;
  } else {
    metricPower.textContent = '--';
    metricPowerMeta.textContent = 'valeur indisponible';
  }

  if (isFiniteMetric(telemetry.gps_lat) && isFiniteMetric(telemetry.gps_lon)) {
    metricGps.textContent = `${telemetry.gps_lat.toFixed(5)}, ${telemetry.gps_lon.toFixed(5)}`;
    metricGpsMeta.textContent = isFiniteMetric(telemetry.gps_alt_m) ? `${telemetry.gps_alt_m.toFixed(1)} m altitude` : 'altitude indisponible';
    gpsReadout.textContent = metricGps.textContent;
  } else {
    metricGps.textContent = '--';
    metricGpsMeta.textContent = 'aucun fix valide';
    gpsReadout.textContent = '--';
  }

  const droneId = telemetry.drone_id || '--';
  metricDrone.textContent = droneId;
  metricDroneMeta.textContent = lastFrameLoadedAt ? 'session active' : 'attente de donnees';
  jetsonId.textContent = droneId;
}

async function fetchTelemetry() {
  try {
    const res = await fetch(`/api/telemetry?ts=${Date.now()}`, { cache: 'no-store' });
    if (!res.ok) return;
    telemetry = await res.json();
    renderTelemetry();
  } catch (_) {
    // ignore
  }
}

async function fetchLogs() {
  try {
    const res = await fetch(`/api/logs?lines=200&ts=${Date.now()}`, { cache: 'no-store' });
    if (!res.ok) return;
    const data = await res.json();
    renderLogLines(data.lines || []);
  } catch (_) {
    // ignore
  }
}

function showFrame(index) {
  if (index < 0 || index >= frameHistory.length) return;
  frameIndex = index;
  const item = frameHistory[frameIndex];
  videoFrame.src = item.url;
  frameMeta.textContent = `Frame ${item.frameId}`;
  frameBytes.textContent = item.bytes ? `${item.bytes} B` : '--';
  if (item.loadedAt) {
    lastFrameLoadedAt = item.loadedAt;
  }
  updateFreshness();
}

async function loadFrameForMeta(meta) {
  if (!live || paused || frameFetchInFlight) return;
  if (!meta || !Number.isFinite(meta.frame_id)) return;
  if (meta.frame_id === lastLoadedFrameId) return;
  if (Date.now() - lastFrameRequestAt < maxFrameFetchRateMs) return;

  const frameId = meta.frame_id;
  const bytes = Number.isFinite(meta.bytes) ? meta.bytes : 0;
  const url = `/api/frame?frame_id=${encodeURIComponent(frameId)}&ts=${Date.now()}`;
  frameFetchInFlight = true;
  lastFrameRequestAt = Date.now();
  try {
    const res = await fetch(url, { cache: 'no-store' });
    if (!res.ok) {
      throw new Error(`frame-http-${res.status}`);
    }
    const blob = await res.blob();
    const objectUrl = URL.createObjectURL(blob);
    const loadedAt = Date.now();

    if (activeObjectUrl) {
      URL.revokeObjectURL(activeObjectUrl);
    }
    activeObjectUrl = objectUrl;

    frameFetchInFlight = false;
    frameErrorCount = 0;
    lastLoadedFrameId = frameId;
    lastFrameLoadedAt = loadedAt;
    frameHistory.push({ frameId, bytes, url: objectUrl, loadedAt });
    if (frameHistory.length > maxHistory) {
      const dropped = frameHistory.shift();
      if (dropped && dropped.url && dropped.url.startsWith('blob:') && dropped.url !== activeObjectUrl) {
        URL.revokeObjectURL(dropped.url);
      }
    }
    frameIndex = frameHistory.length - 1;
    videoFrame.src = objectUrl;
    frameMeta.textContent = `Frame ${frameId}`;
    frameBytes.textContent = bytes ? `${bytes} B` : '--';
    updateFreshness();
  } catch (_) {
    frameFetchInFlight = false;
    frameErrorCount += 1;
    if (frameErrorCount >= 3) {
      setVideoState('NO FEED', 'Les metadonnees arrivent, mais l image decodee est momentanement indisponible.');
    } else {
      setVideoState('SYNC', 'Reessai de chargement image en cours.');
    }
  }
}

async function fetchFrameMeta() {
  try {
    const res = await fetch(`/api/frame_meta?ts=${Date.now()}`, { cache: 'no-store' });
    if (!res.ok) return;
    const data = await res.json();
    if (!Number.isFinite(data.frame_id)) return;

    lastFrameMetaAt = Date.now();
    frameMeta.textContent = `Frame ${data.frame_id}`;
    frameBytes.textContent = Number.isFinite(data.bytes) ? `${data.bytes} B` : '--';

    if (lastSeenMetaFrameId !== data.frame_id) {
      lastSeenMetaFrameId = data.frame_id;
      await loadFrameForMeta(data);
    } else {
      updateFreshness();
    }
  } catch (_) {
    // ignore
  }
}

async function fetchTree(path = currentTreePath) {
  try {
    const res = await fetch(`/api/tree?path=${encodeURIComponent(path)}`, { cache: 'no-store' });
    if (!res.ok) return;
    const data = await res.json();
    currentTreePath = data.path || '';
    filePath.textContent = data.path || '/';
    fileTree.replaceChildren();
    if (data.parent !== null && data.parent !== undefined) {
      fileTree.appendChild(createFileItem('..', 'parent', 'dir', () => fetchTree(data.parent)));
    }
    (data.items || []).forEach(item => {
      fileTree.appendChild(
        createFileItem(
          item.name,
          item.meta || '',
          item.type,
          item.type === 'dir' ? () => fetchTree(item.path) : null,
        ),
      );
    });
  } catch (_) {
    // ignore
  }
}

document.getElementById('btnBack').addEventListener('click', () => {
  if (frameHistory.length === 0) return;
  live = false;
  paused = true;
  showFrame(Math.max(0, frameIndex - 1));
});

document.getElementById('btnPause').addEventListener('click', () => {
  paused = !paused;
  updateFreshness();
});

document.getElementById('btnLive').addEventListener('click', () => {
  live = true;
  paused = false;
  if (frameHistory.length > 0) {
    showFrame(frameHistory.length - 1);
  } else {
    updateFreshness();
  }
});

videoFrame.addEventListener('load', () => {
  videoFallback.hidden = true;
});

videoFrame.addEventListener('error', () => {
  setVideoState('NO FEED', 'Le navigateur ne parvient pas a decoder la derniere image disponible.');
});

const globeCanvas = document.getElementById('globeCanvas');
const gctx = globeCanvas.getContext('2d');
let rot = 0;

function projectPoint(lat, lon, spin) {
  const phi = (lat * Math.PI) / 180;
  const theta = (lon * Math.PI) / 180 + spin;
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

  gctx.beginPath();
  gctx.arc(cx, cy, r, 0, Math.PI * 2);
  gctx.strokeStyle = 'rgba(39, 93, 122, 0.16)';
  gctx.lineWidth = 1.4;
  gctx.stroke();

  gctx.strokeStyle = 'rgba(39, 93, 122, 0.28)';
  gctx.lineWidth = 1;
  for (let lat = -60; lat <= 60; lat += 30) {
    gctx.beginPath();
    let started = false;
    for (let lon = 0; lon <= 360; lon += 8) {
      const p = projectPoint(lat, lon, rot);
      if (p.z < 0) continue;
      const x = cx + p.x * r;
      const y = cy + p.y * r;
      if (!started) {
        gctx.moveTo(x, y);
        started = true;
      } else {
        gctx.lineTo(x, y);
      }
    }
    gctx.stroke();
  }

  for (let lon = 0; lon < 180; lon += 30) {
    gctx.beginPath();
    let started = false;
    for (let lat = -90; lat <= 90; lat += 5) {
      const p = projectPoint(lat, lon, rot);
      if (p.z < 0) continue;
      const x = cx + p.x * r;
      const y = cy + p.y * r;
      if (!started) {
        gctx.moveTo(x, y);
        started = true;
      } else {
        gctx.lineTo(x, y);
      }
    }
    gctx.stroke();
  }

  const lat = telemetry.gps_lat ?? 0;
  const lon = telemetry.gps_lon ?? 0;
  if (isFiniteMetric(lat) && isFiniteMetric(lon)) {
    const p = projectPoint(lat, lon, rot);
    if (p.z > 0) {
      const x = cx + p.x * r;
      const y = cy + p.y * r;
      gctx.beginPath();
      gctx.arc(x, y, 5, 0, Math.PI * 2);
      gctx.fillStyle = '#275d7a';
      gctx.fill();
      gctx.beginPath();
      gctx.arc(x, y, 13, 0, Math.PI * 2);
      gctx.strokeStyle = 'rgba(39, 93, 122, 0.45)';
      gctx.stroke();
    }
  }

  rot += 0.003;
  requestAnimationFrame(drawGlobe);
}

function updateDerivedStatus() {
  if (lastFrameMetaAt && Date.now() - lastFrameMetaAt < 4000) {
    setSessionState('Connectee');
  } else if (lastFrameMetaAt) {
    setSessionState('Degradee');
  } else {
    setSessionState('Attente');
  }
  updateFreshness();
}

setInterval(fetchTelemetry, 1500);
setInterval(fetchLogs, 2000);
setInterval(fetchFrameMeta, 900);
setInterval(() => fetchTree(), 15000);
setInterval(updateDerivedStatus, 1000);

drawGlobe();
fetchTelemetry();
fetchLogs();
fetchFrameMeta();
fetchTree('');
updateDerivedStatus();
