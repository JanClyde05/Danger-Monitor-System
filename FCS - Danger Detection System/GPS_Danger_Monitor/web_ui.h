#pragma once
// web_ui.h  –  Embedded web dashboard for GPS Danger Monitor v2
// Served at "/" by the ESP32 WebServer in AP mode.

const char* WEB_INDEX_HTML = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>GPS Danger Monitor</title>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <style>
    :root {
      --bg:     #080e20;
      --card:   #0f1730;
      --kv-bg:  #0a1228;
      --accent: #4d7cff;
      --ok:     #5de0a0;
      --warn:   #ffd166;
      --bad:    #ff6b6b;
      --text:   #d8e8ff;
      --muted:  #6a80b0;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
    }
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body   { background: var(--bg); color: var(--text); min-height: 100vh; }
    header {
      padding: 14px 20px;
      background: #0b1327;
      border-bottom: 1px solid #1c2c58;
      display: flex; align-items: center; gap: 12px;
    }
    header svg { flex-shrink: 0; }
    h1 { font-size: 17px; letter-spacing: .3px; }
    main   { padding: 20px; max-width: 920px; margin: 0 auto; }
    .card  {
      background: var(--card);
      border-radius: 16px;
      padding: 18px;
      margin-bottom: 16px;
      box-shadow: 0 8px 32px rgba(0,0,0,.35);
    }
    .grid  {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 10px;
    }
    .kv    {
      background: var(--kv-bg);
      padding: 12px 14px;
      border-radius: 12px;
      border: 1px solid #1a2650;
    }
    .kv b  {
      display: block;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: .8px;
      color: var(--muted);
      margin-bottom: 5px;
    }
    .kv span { font-size: 18px; font-weight: 600; }
    .ok   { color: var(--ok);   }
    .bad  { color: var(--bad);  }
    .warn { color: var(--warn); }
    .row  { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
    button {
      background: #1e3070;
      color: #c8d8ff;
      border: 1px solid #2d4090;
      padding: 10px 18px;
      border-radius: 10px;
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
      transition: background .15s;
    }
    button:hover { background: #263d8a; }
    #disarmBtn  { background: #501830; border-color: #802040; color: #ffb0b0; }
    #disarmBtn:hover { background: #641e3a; }
    .footer { color: var(--muted); font-size: 12px; margin-top: 10px; }
    .tips li { margin: 6px 0 0 18px; font-size: 14px; color: #aabce0; }
    #fallbar {
      height: 8px; border-radius: 4px;
      background: #1a2650;
      overflow: hidden;
      margin-top: 8px;
    }
    #fallinner {
      height: 100%; width: 0%;
      background: var(--accent);
      border-radius: 4px;
      transition: width .3s, background .3s;
    }
    #fallabel { font-size: 12px; color: var(--muted); margin-top: 4px; }
  </style>
</head>
<body>
<header>
  <!-- shield icon -->
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#4d7cff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
  </svg>
  <h1>GPS Danger Monitor v2</h1>
</header>

<main>

  <!-- Controls -->
  <div class="card">
    <div class="row">
      <button id="armBtn">&#9632; Arm</button>
      <button id="disarmBtn">&#9633; Disarm</button>
      <button id="testBtn">&#8594; Send Test NRF</button>
    </div>
    <div class="footer" style="margin-top:12px">
      AP: <b>GPS Danger Monitor</b> &nbsp;&#8226;&nbsp; No password
      &nbsp;&#8226;&nbsp; IP: <b>192.168.4.1</b>
    </div>
  </div>

  <!-- Live data -->
  <div class="card">
    <div class="grid">
      <div class="kv"><b>Armed</b>      <span id="armed">—</span></div>
      <div class="kv"><b>GPS Fix</b>    <span id="fix">—</span></div>
      <div class="kv"><b>Latitude</b>   <span id="lat">—</span></div>
      <div class="kv"><b>Longitude</b>  <span id="lng">—</span></div>
      <div class="kv"><b>Roll (°)</b>   <span id="roll">—</span></div>
      <div class="kv"><b>Pitch (°)</b>  <span id="pitch">—</span></div>
      <div class="kv"><b>Accel |g|</b>  <span id="amag">—</span></div>
      <div class="kv"><b>NRF24</b>      <span id="nrf">—</span></div>
    </div>
  </div>

  <!-- Fall state -->
  <div class="card">
    <div style="font-size:13px; color:var(--muted); text-transform:uppercase; letter-spacing:.8px; margin-bottom:8px">
      Fall Detection Pipeline
    </div>
    <div id="fallbar"><div id="fallinner"></div></div>
    <div id="fallabel">Idle – monitoring…</div>
    <div class="kv" style="margin-top:12px">
      <b>Last Event</b><span id="event">—</span>
    </div>
  </div>

  <!-- Tips -->
  <div class="card">
    <div style="font-weight:600; margin-bottom:6px">How it works</div>
    <ul class="tips">
      <li>Alert fires only after: <b>Freefall → Impact spike → Stillness</b> – cutting false alarms from walking.</li>
      <li>GPS coordinates are sent over NRF24 to the Pro Micro, which opens your Netlify alert page.</li>
      <li>Last-known location is used if GPS signal is lost at time of accident.</li>
    </ul>
  </div>

</main>

<script>
const q = s => document.querySelector(s);

const FALL_LABELS = [
  'Idle – monitoring…',
  'Stage 1: Freefall detected…',
  'Stage 2: Awaiting impact spike…',
  'Stage 3: Verifying stillness…',
  'CONFIRMED – alert sent!'
];
const FALL_PCTS = [0, 30, 60, 85, 100];
const FALL_COLORS = ['#4d7cff','#ffd166','#ff9840','#ff6b6b','#ff3b3b'];

async function refresh() {
  try {
    const j = await fetch('/status.json').then(r => r.json());

    q('#armed').textContent = j.armed ? 'Yes' : 'No';
    q('#armed').className   = j.armed ? 'ok' : 'bad';

    q('#fix').textContent = j.hasFix ? 'Yes (locked)' : 'Searching…';
    q('#fix').className   = j.hasFix ? 'ok' : 'warn';

    q('#lat').textContent   = j.lat.toFixed(6);
    q('#lng').textContent   = j.lng.toFixed(6);
    q('#roll').textContent  = j.roll.toFixed(2);
    q('#pitch').textContent = j.pitch.toFixed(2);
    q('#amag').textContent  = j.aMag.toFixed(3) + ' g';

    q('#nrf').textContent = j.nrfReady ? 'Ready' : 'Not ready';
    q('#nrf').className   = j.nrfReady ? 'ok' : 'bad';

    q('#event').textContent = j.lastEvent || '—';

    // Fall pipeline progress bar
    const fs = Math.min(Math.max(parseInt(j.fallState) || 0, 0), 4);
    q('#fallinner').style.width    = FALL_PCTS[fs] + '%';
    q('#fallinner').style.background = FALL_COLORS[fs];
    q('#fallabel').textContent     = FALL_LABELS[fs];
  } catch(e) {
    q('#event').textContent = 'Connection lost…';
  }
}

setInterval(refresh, 900);
refresh();

q('#armBtn').onclick    = () => fetch('/arm').then(refresh);
q('#disarmBtn').onclick = () => fetch('/disarm').then(refresh);
q('#testBtn').onclick   = () =>
  fetch('/test-nrf')
    .then(r => r.text())
    .then(t => { q('#event').textContent = t; refresh(); })
    .catch(() => refresh());
</script>
</body>
</html>
)HTML";
