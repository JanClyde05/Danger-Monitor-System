#pragma once
// web_ui.h  –  GPS Danger Monitor v3.0  –  Diagnostics + Simulation Edition
// Served at "/" in AP mode.  Three tabs: Monitor | Diagnostics | Simulation

const char* WEB_INDEX_HTML = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>GPS Danger Monitor v3</title>
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
      --skid:   #ff9f40;
      --impact: #d966ff;
      --text:   #d8e8ff;
      --muted:  #6a80b0;
      --border: rgba(77,124,255,0.15);
      font-family: system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
    }
    *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
    body{background:var(--bg);color:var(--text);min-height:100vh}

    /* ── Header ── */
    header{
      padding:12px 18px;
      background:#0b1327;
      border-bottom:1px solid #1c2c58;
      display:flex;align-items:center;gap:10px;
      position:sticky;top:0;z-index:100;
    }
    h1{font-size:16px;letter-spacing:.3px;flex:1}

    /* ── Tab bar ── */
    .tabs{
      display:flex;gap:2px;
      background:#060e1e;
      padding:10px 18px 0;
      border-bottom:1px solid var(--border);
    }
    .tab{
      padding:8px 18px;border-radius:8px 8px 0 0;
      font-size:13px;font-weight:600;cursor:pointer;
      color:var(--muted);border:1px solid transparent;
      border-bottom:none;transition:all .15s;
      background:transparent;
    }
    .tab.active{
      color:var(--text);background:#0f1730;
      border-color:var(--border);border-bottom-color:#0f1730;
    }
    .tab:hover:not(.active){color:var(--text)}

    /* ── Page sections ── */
    .page{display:none;padding:18px;max-width:960px;margin:0 auto}
    .page.active{display:block}

    /* ── Cards ── */
    .card{
      background:var(--card);border-radius:14px;padding:16px;
      margin-bottom:14px;border:1px solid var(--border);
    }
    .card-title{
      font-size:11px;text-transform:uppercase;letter-spacing:.8px;
      color:var(--muted);margin-bottom:12px;font-weight:700;
    }
    .grid{
      display:grid;
      grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:8px;
    }
    .kv{
      background:var(--kv-bg);padding:10px 12px;border-radius:10px;
      border:1px solid rgba(77,124,255,0.1);
    }
    .kv b{display:block;font-size:10px;text-transform:uppercase;
      letter-spacing:.8px;color:var(--muted);margin-bottom:4px}
    .kv span{font-size:17px;font-weight:600}
    .ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
    .skid-c{color:var(--skid)}.impact-c{color:var(--impact)}

    .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
    button{
      background:#1e3070;color:#c8d8ff;border:1px solid #2d4090;
      padding:9px 16px;border-radius:9px;font-size:13px;font-weight:600;
      cursor:pointer;transition:background .15s;
    }
    button:hover{background:#263d8a}
    button:disabled{opacity:.4;cursor:not-allowed}
    .btn-danger{background:#501830;border-color:#802040;color:#ffb0b0}
    .btn-danger:hover{background:#641e3a}
    .btn-skid{background:#3a2000;border-color:#7a5000;color:var(--skid)}
    .btn-skid:hover{background:#4a2c00}
    .btn-fall{background:#001a40;border-color:#003080;color:#6ab8ff}
    .btn-fall:hover{background:#002050}
    .btn-impact{background:#280040;border-color:#600090;color:var(--impact)}
    .btn-impact:hover{background:#380050}
    .btn-stop{background:#2a0a0a;border-color:#600;color:#ff8080}
    .footer{color:var(--muted);font-size:12px;margin-top:8px}

    /* ── Fall pipeline bar ── */
    #fallbar{height:7px;border-radius:4px;background:#1a2650;overflow:hidden;margin-top:8px}
    #fallinner{height:100%;width:0%;background:var(--accent);border-radius:4px;transition:width .3s,background .3s}
    #fallabel{font-size:12px;color:var(--muted);margin-top:4px}

    /* ── Burst test table ── */
    .burst-table{width:100%;border-collapse:collapse;font-size:12px;margin-top:10px}
    .burst-table th{
      color:var(--muted);font-size:10px;text-transform:uppercase;letter-spacing:.5px;
      padding:5px 8px;text-align:left;border-bottom:1px solid var(--border);
    }
    .burst-table td{padding:5px 8px;border-bottom:1px solid rgba(77,124,255,0.06)}
    .burst-table tr:last-child td{border-bottom:none}
    .burst-ok{color:var(--ok)}.burst-fail{color:var(--bad)}
    .delay-bar{
      display:inline-block;height:6px;border-radius:3px;
      background:var(--accent);min-width:2px;vertical-align:middle;
    }
    .burst-summary{
      display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;
      margin-top:10px;
    }

    /* ── Waveform canvas ── */
    #waveCanvas{
      width:100%;height:120px;border-radius:10px;
      background:#070d1e;border:1px solid var(--border);
      display:block;
    }
    .wave-legend{
      display:flex;gap:14px;flex-wrap:wrap;margin-top:6px;font-size:11px;color:var(--muted);
    }
    .legend-dot{
      display:inline-block;width:16px;height:2px;vertical-align:middle;margin-right:4px;border-radius:1px;
    }

    /* ── Sim status ── */
    #simStatus{
      background:#070d1e;border:1px solid var(--border);
      border-radius:10px;padding:10px 14px;font-size:13px;
      color:var(--muted);margin-top:10px;font-family:monospace;
      min-height:36px;
    }
    #simStatus.active{color:var(--warn)}

    .threshold-tag{
      display:inline-block;padding:2px 8px;border-radius:4px;
      font-size:11px;font-family:monospace;margin:2px 0;
    }
    .tips li{margin:5px 0 0 18px;font-size:13px;color:#aabce0}
    .badge{
      display:inline-flex;align-items:center;gap:5px;
      background:rgba(77,124,255,0.1);border:1px solid rgba(77,124,255,0.25);
      color:var(--accent);font-size:11px;padding:3px 8px;border-radius:5px;
      font-family:monospace;font-weight:700;
    }
  </style>
</head>
<body>

<header>
  <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#4d7cff"
       stroke-width="2" stroke-linecap="round" stroke-linejoin="round" flex-shrink="0">
    <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
  </svg>
  <h1>GPS Danger Monitor v3</h1>
  <span class="badge" id="hdrEvent">—</span>
</header>

<div class="tabs">
  <button class="tab active" onclick="showTab('monitor')">📡 Monitor</button>
  <button class="tab" onclick="showTab('diagnostics')">🔬 Diagnostics</button>
  <button class="tab" onclick="showTab('simulation')">🎮 Simulation</button>
</div>

<!-- ══════════════════════════ MONITOR TAB ══════════════════════════ -->
<div class="page active" id="tab-monitor">

  <!-- Controls -->
  <div class="card">
    <div class="row">
      <button onclick="fetch('/arm').then(refreshStatus)">&#9632; Arm</button>
      <button class="btn-danger" onclick="fetch('/disarm').then(refreshStatus)">&#9633; Disarm</button>
      <button onclick="singleNRFTest()">&#8594; NRF Single Test</button>
    </div>
    <div class="footer" style="margin-top:10px">
      AP: <b>GPS Danger Monitor</b> &nbsp;•&nbsp; No password &nbsp;•&nbsp; IP: <b>192.168.4.1</b>
    </div>
  </div>

  <!-- Live data -->
  <div class="card">
    <div class="card-title">Live Sensor Data</div>
    <div class="grid">
      <div class="kv"><b>Armed</b>       <span id="armed">—</span></div>
      <div class="kv"><b>GPS Fix</b>     <span id="fix">—</span></div>
      <div class="kv"><b>Latitude</b>    <span id="lat">—</span></div>
      <div class="kv"><b>Longitude</b>   <span id="lng">—</span></div>
      <div class="kv"><b>Roll (°)</b>    <span id="roll">—</span></div>
      <div class="kv"><b>Pitch (°)</b>   <span id="pitch">—</span></div>
      <div class="kv"><b>Accel |g|</b>   <span id="amag">—</span></div>
      <div class="kv"><b>Lateral ay</b>  <span id="ayval">—</span></div>
      <div class="kv"><b>Roll Rate gx</b><span id="gxval">—</span></div>
      <div class="kv"><b>NRF24</b>       <span id="nrf">—</span></div>
    </div>
  </div>

  <!-- Fall pipeline -->
  <div class="card">
    <div class="card-title">Fall Detection Pipeline</div>
    <div id="fallbar"><div id="fallinner"></div></div>
    <div id="fallabel">Idle – monitoring…</div>
    <div class="kv" style="margin-top:10px">
      <b>Last Event</b><span id="event" style="font-size:13px">—</span>
    </div>
  </div>

  <!-- How it works -->
  <div class="card">
    <div style="font-weight:600;margin-bottom:6px">Detection Pipeline</div>
    <ul class="tips">
      <li><b>Fall:</b> Freefall (&lt;0.40g) → Impact spike (&gt;3.0g) → Stillness (σ&lt;0.15g/2s)</li>
      <li><b>Skid:</b> Lateral ay &gt;0.70g AND roll-rate gx &gt;50°/s for ≥200ms <em>(motorcycle crash ref)</em></li>
      <li><b>Impact:</b> Spike &gt;4.0g without freefall precondition <em>(construction-site fall ref)</em></li>
      <li>GPS Last-Known-Location used if signal is lost at time of event.</li>
    </ul>
  </div>
</div>

<!-- ══════════════════════════ DIAGNOSTICS TAB ══════════════════════════ -->
<div class="page" id="tab-diagnostics">

  <!-- NRF Burst Test -->
  <div class="card">
    <div class="card-title">NRF24L01 Burst Test — 20 Packets</div>
    <div class="row" style="margin-bottom:12px">
      <button id="burstBtn" onclick="runBurst()">▶ Run 20-Shot Burst</button>
      <span id="burstProg" style="font-size:13px;color:var(--muted)">Idle</span>
    </div>

    <!-- Summary stats (shown after test) -->
    <div class="burst-summary" id="burstSummary" style="display:none">
      <div class="kv"><b>Success Rate</b><span id="bsr">—</span></div>
      <div class="kv"><b>Avg Delay</b>   <span id="bavg">—</span></div>
      <div class="kv"><b>Min Delay</b>   <span id="bmin">—</span></div>
      <div class="kv"><b>Max Delay</b>   <span id="bmax">—</span></div>
    </div>

    <table class="burst-table" id="burstTable">
      <thead>
        <tr>
          <th>#</th><th>Status</th><th>Delay (ms)</th><th>Latency Bar</th>
        </tr>
      </thead>
      <tbody id="burstBody"></tbody>
    </table>
  </div>

  <!-- GPS Fix Stats -->
  <div class="card">
    <div class="card-title">GPS Fix Statistics</div>
    <div class="grid">
      <div class="kv"><b>Fix Count</b>          <span id="gpsFixCount">—</span></div>
      <div class="kv"><b>Avg Time to Fix</b>    <span id="gpsAvgFix">—</span></div>
      <div class="kv"><b>Current Fix</b>         <span id="gpsCurFix">—</span></div>
    </div>
    <div class="footer" style="margin-top:8px">
      Time-to-fix measured from power-on or last lost fix. GPS module: NEO-7M @ 9600 baud.
    </div>
  </div>

  <!-- Fall Detection Stats -->
  <div class="card">
    <div class="card-title">Fall Detection Diagnostics</div>
    <div class="grid">
      <div class="kv"><b>Events Detected</b>   <span id="fallEvents">—</span></div>
      <div class="kv"><b>Confirmed Alerts</b>  <span id="fallConfirmed">—</span></div>
      <div class="kv"><b>Cancelled Events</b>  <span id="fallCanceled">—</span></div>
      <div class="kv"><b>Avg Response Time</b> <span id="fallRespMs">—</span></div>
      <div class="kv"><b>Precision Rate</b><span id="fallFPR">—</span></div>
      <div class="kv"><b>Skid Events</b>       <span id="skidCount" class="skid-c">—</span></div>
      <div class="kv"><b>Direct Impacts</b>    <span id="impactCount" class="impact-c">—</span></div>
    </div>
    <div class="footer" style="margin-top:8px">
      PR =Confirmed / (Canceled + Confirmed) × 100. Higher is better.
      Response time measured from freefall confirmation to FS_CONFIRMED.
    </div>
  </div>
</div>

<!-- ══════════════════════════ SIMULATION TAB ══════════════════════════ -->
<div class="page" id="tab-simulation">

  <!-- Simulation controls -->
  <div class="card">
    <div class="card-title">IMU Event Simulation (Drill Mode – Test NRF sent, no real alert)</div>
    <div class="row">
      <button class="btn-skid"   onclick="runSim('skid')">🛞 Simulate Skid</button>
      <button class="btn-fall"   onclick="runSim('fall')">⬇ Simulate Fall</button>
      <button class="btn-impact" onclick="runSim('impact')">💥 Simulate Impact</button>
      <button class="btn-stop"   onclick="stopSim()">⏹ Stop Sim</button>
    </div>

    <div id="simStatus">Simulation idle — select an event to drill.</div>

    <!-- Threshold reference table -->
    <div style="margin-top:14px;font-size:12px">
      <div style="color:var(--muted);margin-bottom:8px;font-size:11px;text-transform:uppercase;letter-spacing:.8px">
        IMU Threshold Reference (literature-backed)
      </div>
      <table style="width:100%;border-collapse:collapse;font-size:12px">
        <thead>
          <tr style="color:var(--muted);font-size:10px;text-transform:uppercase">
            <th style="text-align:left;padding:4px 8px">Event</th>
            <th style="text-align:left;padding:4px 8px">Threshold</th>
            <th style="text-align:left;padding:4px 8px">Condition</th>
            <th style="text-align:left;padding:4px 8px">Source</th>
          </tr>
        </thead>
        <tbody style="font-family:monospace">
          <tr style="border-top:1px solid var(--border)">
            <td style="padding:5px 8px;color:#6ab8ff">Freefall</td>
            <td style="padding:5px 8px">aMag &lt; <b>0.40</b> g</td>
            <td style="padding:5px 8px">≥ 100 ms</td>
            <td style="padding:5px 8px;color:var(--muted)">SisFall dataset; airbag paper 0.38g</td>
          </tr>
          <tr style="border-top:1px solid var(--border)">
            <td style="padding:5px 8px;color:var(--bad)">Fall Impact</td>
            <td style="padding:5px 8px">aMag &gt; <b>3.00</b> g</td>
            <td style="padding:5px 8px">within 500 ms post-freefall</td>
            <td style="padding:5px 8px;color:var(--muted)">MPU6050 fall studies; Hackster 2.5g</td>
          </tr>
          <tr style="border-top:1px solid var(--border)">
            <td style="padding:5px 8px;color:var(--ok)">Stillness</td>
            <td style="padding:5px 8px">σ(aMag) &lt; <b>0.15</b> g</td>
            <td style="padding:5px 8px">over 2 s post-impact</td>
            <td style="padding:5px 8px;color:var(--muted)">Unconsciousness check (thesis)</td>
          </tr>
          <tr style="border-top:1px solid var(--border)">
            <td style="padding:5px 8px;color:var(--skid)">Skid/Slide</td>
            <td style="padding:5px 8px">ay &gt; <b>0.70</b> g AND gx &gt; <b>50</b> °/s</td>
            <td style="padding:5px 8px">≥ 200 ms</td>
            <td style="padding:5px 8px;color:var(--muted)">Motorcycle crash paper (0.7g, 96% acc)</td>
          </tr>
          <tr style="border-top:1px solid var(--border)">
            <td style="padding:5px 8px;color:var(--impact)">Direct Impact</td>
            <td style="padding:5px 8px">aMag &gt; <b>4.00</b> g</td>
            <td style="padding:5px 8px">spike (no freefall req.)</td>
            <td style="padding:5px 8px;color:var(--muted)">Sensors 2022: LF falls 4–9g range</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>

  <!-- Live waveform -->
  <div class="card">
    <div class="card-title">Live IMU Waveform (aMag — last 80 samples)</div>
    <canvas id="waveCanvas"></canvas>
    <div class="wave-legend">
      <span><span class="legend-dot" style="background:#4d7cff;width:20px;display:inline-block;height:2px"></span> aMag (g)</span>
      <span><span class="legend-dot" style="background:var(--warn);border-top:2px dashed var(--warn);height:0;width:20px;display:inline-block"></span> Freefall 0.40g</span>
      <span><span class="legend-dot" style="background:var(--skid);border-top:2px dashed var(--skid);height:0;width:20px;display:inline-block"></span> Skid lateral 0.70g</span>
      <span><span class="legend-dot" style="background:#ff6060;border-top:2px dashed #ff6060;height:0;width:20px;display:inline-block"></span> Impact 3.00g</span>
      <span><span class="legend-dot" style="background:var(--impact);border-top:2px solid var(--impact);height:0;width:20px;display:inline-block"></span> Direct 4.00g</span>
    </div>
  </div>

</div><!-- end simulation tab -->

<script>
/* ── Tab switching ── */
function showTab(name){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  document.getElementById('tab-'+name).classList.add('active');
  event.target.classList.add('active');
}

/* ── Status refresh ── */
const FALL_LABELS=[
  'Idle – monitoring…','Stage 1: Freefall detected…',
  'Stage 2: Awaiting impact spike…','Stage 3: Verifying stillness…',
  'CONFIRMED – alert sent!'
];
const FALL_PCTS  =[0,30,60,85,100];
const FALL_COLORS=['#4d7cff','#ffd166','#ff9840','#ff6b6b','#ff3b3b'];
const SIM_NAMES  =['','SKID DRILL','FALL DRILL','IMPACT DRILL'];

async function refreshStatus(){
  try{
    const j=await fetch('/status.json').then(r=>r.json());
    const q=s=>document.querySelector(s);

    q('#armed').textContent=j.armed?'Yes':'No';
    q('#armed').className=j.armed?'ok':'bad';
    q('#fix').textContent=j.hasFix?'Yes (locked)':'Searching…';
    q('#fix').className=j.hasFix?'ok':'warn';
    q('#lat').textContent=j.lat.toFixed(6);
    q('#lng').textContent=j.lng.toFixed(6);
    q('#roll').textContent=j.roll.toFixed(2);
    q('#pitch').textContent=j.pitch.toFixed(2);
    q('#amag').textContent=j.aMag.toFixed(3)+' g';
    q('#amag').className=j.aMag>3?'bad':j.aMag<0.4?'warn':'';
    q('#ayval').textContent=j.ay.toFixed(3)+' g';
    q('#ayval').className=Math.abs(j.ay)>0.7?'skid-c':'';
    q('#gxval').textContent=j.gx.toFixed(1)+' °/s';
    q('#gxval').className=Math.abs(j.gx)>50?'skid-c':'';
    q('#nrf').textContent=j.nrfReady?'Ready':'Not ready';
    q('#nrf').className=j.nrfReady?'ok':'bad';
    q('#event').textContent=j.lastEvent||'—';
    q('#hdrEvent').textContent=j.lastEvent||'—';

    const fs=Math.min(Math.max(parseInt(j.fallState)||0,0),4);
    q('#fallinner').style.width=FALL_PCTS[fs]+'%';
    q('#fallinner').style.background=FALL_COLORS[fs];
    q('#fallabel').textContent=FALL_LABELS[fs];

    // Sim status
    const ss=q('#simStatus');
    if(j.simEvent>0){
      ss.textContent='▶ '+SIM_NAMES[j.simEvent]+' in progress… | '+j.lastEvent;
      ss.className='active';
    } else {
      ss.textContent='Simulation idle — select an event to drill.';
      ss.className='';
    }

    // Push aMag to waveform buffer
    waveBuf.push(j.aMag);
    ayBuf.push(Math.abs(j.ay));
    if(waveBuf.length>80) waveBuf.shift();
    if(ayBuf.length>80)   ayBuf.shift();
    drawWave();
  } catch(e){
    document.querySelector('#event').textContent='Connection lost…';
  }
}

/* ── Diag stats refresh ── */
async function refreshDiag(){
  try{
    const d=await fetch('/diag-stats.json').then(r=>r.json());
    const q=s=>document.querySelector(s);
    const ms=v=>v?v+' ms':'—';

    q('#gpsFixCount').textContent=d.gpsFixCount||'0';
    q('#gpsAvgFix').textContent=ms(d.gpsAvgFixMs);
    q('#gpsCurFix').textContent=d.gpsFixCount>0?'Acquired':'Searching…';
    q('#gpsCurFix').className=d.gpsFixCount>0?'ok':'warn';

    q('#fallEvents').textContent=d.fallEvents||'0';
    q('#fallConfirmed').textContent=d.fallConfirmed||'0';
    q('#fallCanceled').textContent=d.fallCanceled||'0';
    q('#fallRespMs').textContent=ms(d.fallAvgRespMs);
    q('#fallFPR').textContent=d.fallFPR.toFixed(1)+'%';
    q('#fallFPR').className=d.fallFPR>20?'bad':d.fallFPR>10?'warn':'ok';
    q('#skidCount').textContent=d.skidCount||'0';
    q('#impactCount').textContent=d.impactCount||'0';

    // Poll burst results if running
    if(d.burstRunning) pollBurst();
  } catch(e){}
}

setInterval(refreshStatus,900);
setInterval(refreshDiag,1500);
refreshStatus(); refreshDiag();

/* ── Single NRF test (legacy) ── */
function singleNRFTest(){
  fetch('/test-nrf').then(r=>r.text())
    .then(t=>{document.querySelector('#event').textContent=t; refreshStatus();})
    .catch(()=>refreshStatus());
}

/* ══════════════════════════════════════════════
   NRF BURST TEST
══════════════════════════════════════════════ */
let burstPolling=false;

async function runBurst(){
  const btn=document.getElementById('burstBtn');
  btn.disabled=true;
  document.getElementById('burstBody').innerHTML='';
  document.getElementById('burstSummary').style.display='none';
  document.getElementById('burstProg').textContent='Starting…';

  try{
    const r=await fetch('/run-burst');
    if(!r.ok){ document.getElementById('burstProg').textContent='Error: '+await r.text(); btn.disabled=false; return; }
    burstPolling=true;
    pollBurst();
  } catch(e){
    document.getElementById('burstProg').textContent='Connection error';
    btn.disabled=false;
  }
}

async function pollBurst(){
  try{
    const d=await fetch('/burst-results.json').then(r=>r.json());
    const body=document.getElementById('burstBody');
    const prog=document.getElementById('burstProg');
    prog.textContent=d.running?'Running… '+d.done+'/20':'Complete';

    body.innerHTML='';
    const maxDelay=Math.max(...d.results.map(r=>r.ms),1);
    let okCount=0, totalMs=0, minMs=9999, maxMs=0;

    d.results.forEach((r,i)=>{
      if(i>=d.done) return;
      const tr=document.createElement('tr');
      const barW=Math.max(Math.round((r.ms/maxDelay)*80),2);
      tr.innerHTML=`
        <td style="color:var(--muted)">${String(i+1).padStart(2,'0')}</td>
        <td class="${r.ok?'burst-ok':'burst-fail'}">${r.ok?'✓ ACK':'✗ FAIL'}</td>
        <td>${r.ok?r.ms+' ms':'—'}</td>
        <td><span class="delay-bar" style="width:${barW}px;background:${r.ok?'var(--accent)':'var(--bad)'}"></span></td>
      `;
      body.appendChild(tr);
      if(r.ok){ okCount++; totalMs+=r.ms; if(r.ms<minMs)minMs=r.ms; if(r.ms>maxMs)maxMs=r.ms; }
    });

    if(!d.running && d.done>=20){
      burstPolling=false;
      document.getElementById('burstBtn').disabled=false;
      document.getElementById('burstSummary').style.display='grid';
      const sr=(okCount/20*100).toFixed(0);
      document.getElementById('bsr').textContent=sr+'%';
      document.getElementById('bsr').className=sr>=90?'ok':sr>=70?'warn':'bad';
      document.getElementById('bavg').textContent=okCount?Math.round(totalMs/okCount)+' ms':'—';
      document.getElementById('bmin').textContent=okCount?minMs+' ms':'—';
      document.getElementById('bmax').textContent=okCount?maxMs+' ms':'—';
    } else if(d.running){
      setTimeout(pollBurst,300);
    }
  } catch(e){
    if(burstPolling) setTimeout(pollBurst,500);
  }
}

/* ══════════════════════════════════════════════
   SIMULATION
══════════════════════════════════════════════ */
function runSim(event){
  fetch('/simulate?event='+event)
    .then(()=>{ refreshStatus(); })
    .catch(()=>{});
}
function stopSim(){
  fetch('/sim-stop').then(()=>refreshStatus()).catch(()=>{});
}

/* ══════════════════════════════════════════════
   WAVEFORM CANVAS
══════════════════════════════════════════════ */
const waveBuf=[];
const ayBuf=[];
const canvas=document.getElementById('waveCanvas');
const ctx2=canvas.getContext('2d');

const THRESH_FREEFALL=0.40;
const THRESH_SKID    =0.70;
const THRESH_IMPACT  =3.00;
const THRESH_DIRECT  =4.00;
const Y_MAX=6.0;

function drawWave(){
  const W=canvas.offsetWidth||600, H=canvas.offsetHeight||120;
  canvas.width=W; canvas.height=H;
  const g=ctx2;
  g.clearRect(0,0,W,H);

  function yOf(v){ return H - (v/Y_MAX)*H; }

  // Grid
  g.strokeStyle='rgba(77,124,255,0.07)';
  g.lineWidth=1;
  for(let gv=1;gv<=5;gv++){
    const y=yOf(gv);
    g.beginPath(); g.moveTo(0,y); g.lineTo(W,y); g.stroke();
  }

  // Threshold lines
  const thresholds=[
    [THRESH_FREEFALL,'rgba(255,209,102,0.7)','dashed'],
    [THRESH_SKID,    'rgba(255,159,64,0.6)', 'dashed'],
    [THRESH_IMPACT,  'rgba(255,100,100,0.7)','dashed'],
    [THRESH_DIRECT,  'rgba(210,102,255,0.8)','solid'],
  ];
  thresholds.forEach(([v,col,style])=>{
    const y=yOf(v);
    g.strokeStyle=col; g.lineWidth=1;
    g.setLineDash(style==='dashed'?[4,4]:[]);
    g.beginPath(); g.moveTo(0,y); g.lineTo(W,y); g.stroke();
  });
  g.setLineDash([]);

  // aMag waveform
  if(waveBuf.length<2){ return; }
  const step=W/(79);
  g.strokeStyle='#4d7cff'; g.lineWidth=2;
  g.beginPath();
  waveBuf.forEach((v,i)=>{
    const x=i*step, y=yOf(Math.min(v,Y_MAX));
    i===0?g.moveTo(x,y):g.lineTo(x,y);
  });
  g.stroke();

  // Y-axis labels
  g.fillStyle='rgba(106,128,176,0.8)'; g.font='9px monospace';
  [0,1,2,3,4,5,6].forEach(v=>{ g.fillText(v+'g',3,yOf(v)-2); });
}

window.addEventListener('resize',drawWave);
</script>
</body>
</html>
)HTML";
