# GPS Danger Monitor System – Full Code Documentation (v4.1)
##### BY: JAN CLYDE T. TALOSIG
##### **NEW IN v4.1: Physical Button Two-State Toggle + GPS Share / Open in Maps**
##### **NEW IN v4.0: Physical Disarm Button + False Alarm NRF Broadcast**
##### **NEW IN v3.x / Web Portal: Confirmed Accident Database + Diagnosis Notes + SOS Audio**

---

# 1. SYSTEM OVERVIEW

## 1.1 Purpose

The system is a **wearable/embedded safety monitor** that detects:

* Human falls (high confidence – three-stage FSM)
* Skid / slide events (lateral acceleration + roll rate)
* Direct high-G impacts (no freefall precondition)
* Environmental hazards (ground shock / structural instability / wave motion)

It then:

1. Gets GPS coordinates (last-known-location strategy)
2. Sends them via NRF24L01 with a token-prefixed payload
3. Triggers an SOS alert (LED + buzzer, Morse SOS pattern)
4. Opens a **web-based emergency profile with QR code quick-dial** via HID receiver
5. Allows the user to **cancel a false alarm** via physical button or web dashboard, which broadcasts a `FA,TOKEN,LAT,LNG` NRF packet to notify the receiver

---

## 1.2 System Components

### Transmitter (ESP32) – v4.1

* Sensors:
  * MPU-6050 (motion / orientation)
  * GPS module via TinyGPS++ (UART1, RX=16, TX=17)
* Communication:
  * NRF24L01 (wireless, CE=2, CSN=15)
* Interface:
  * Web dashboard in AP mode (three tabs: Monitor, Diagnostics, Simulation)
  * Physical disarm/re-arm button on GPIO 0 (BOOT button)

### Receiver (Pro Micro) – v2.3

* NRF24L01 (receiver, CE=9, CSN=10)
* HID Keyboard emulation (opens Netlify URL in browser)
* Handles three payload types: EMERGENCY, TEST, FALSE ALARM

### Web Layer

* ESP32 embedded dashboard (AP mode, 192.168.4.1)
* Netlify emergency portal (multi-page SPA with router)

---

# 2. ABSTRACT PROGRAM FLOW (SUMMARY)

## HIGH-LEVEL FLOW

```
BOOT → INIT HARDWARE → CALIBRATION → RUN LOOP
         ↓
     READ SENSORS (IMU + GPS)
         ↓
    APPLY SIM OVERRIDE (if drill mode)
         ↓
  UPDATE FALL-IMPACT-STILLNESS FSM
         ↓
  CHECK SKID / DIRECT IMPACT DETECTORS
         ↓
  CHECK ENVIRONMENTAL ANALYSIS (every 1 s)
         ↓
  SEND NRF ALERT + SOS (if triggered)
         ↓
  POLL PHYSICAL BUTTON (two-state toggle)
         ↓
  UPDATE WEB UI + BURST TEST (non-blocking)
         ↓
       REPEAT
```

---

## CORE LOGIC PIPELINES

### FALL DETECTION PIPELINE

```
[FS_IDLE]
   ↓ A_m < 0.40 g
[FS_FREEFALL]
   ↓ sustained ≥ 100 ms
[FS_WAIT_IMPACT]
   ↓ A_m > 3.00 g within 500 ms
[FS_STILLNESS]
   ↓ σ(A_m) < 0.15 g over 2 s
[FS_CONFIRMED → ALERT]
```

All stages must pass — eliminates false positives from jumping, device drops, or sleeping.

---

### SECONDARY DETECTORS (Parallel to FSM)

```
SKID / SLIDE:
  ay > 2.0 g  AND  gx > 330 °/s  sustained ≥ 200 ms
  → Ref: motorcycle crash paper, 96% accuracy

DIRECT IMPACT:
  A_m spike > 4.50 g  (no freefall precondition)
  5 s debounce between events
  → Ref: construction-site LF falls 4–9 g (Sensors 2022)
```

---

### EMERGENCY RESPONSE PIPELINE

```
[ACCIDENT DETECTED]
   ↓
[NRF PAYLOAD: "TOKEN,LAT,LNG"]
   ↓
[RECEIVER HID TRIGGERS URL]
   ↓
[NETLIFY PROFILE PAGE OPENS]
   ↓
[MEDICAL INFO + QR CODE DISPLAYED]
   ↓
[RESPONDER SCANS QR → tel: DIALS CONTACT]
   ↓ (optional)
[RESPONDER CONFIRMS ACCIDENT → DATABASE ENTRY]
```

---

### FALSE ALARM CANCELLATION PIPELINE (v4.0+)

```
[ALERT ACTIVE]
   ↓ (user presses physical button OR web False Alarm button)
[stopSOSAlert() — LED/buzzer off]
   ↓
[nrfSendFalseAlarm(): "FA,TOKEN,LAT,LNG"]
   ↓
[RECEIVER HID TRIGGERS FALSE ALARM URL]
   ↓
[NETLIFY: green "USER IS SAFE" page]
   ↓
[RESCUER INFORMED — NO RESPONSE NEEDED]
```

---

# 3. DETAILED MODULE EXPLANATION

## 3.1 SETUP PHASE

```
setup():
  Start Serial (115200)
  Start GPS UART1 (9600 baud, RX=16, TX=17)
  Initialize I2C (Wire.begin)
  Wake MPU-6050 (write 0x00 to PWR_MGMT_1)
  Calibrate MPU (3 seconds, 200 samples)
  Record gpsSearchStart timestamp
  Start WiFi AP (SSID: "GPS Danger Monitor", open, IP: 192.168.4.1)
  Initialize NRF24 (CE=2, CSN=15, PA_HIGH, 32-byte payload)
  Register all WebServer routes
  Configure GPIO: LED (4), Buzzer (5), Button (0 INPUT_PULLUP)
```

---

## 3.2 MAIN LOOP STRUCTURE

```
loop():
  Compute delta time dt
  server.handleClient()
  Poll physical button (50 ms debounce, two-state toggle)
  Run NRF burst test step (non-blocking, if pending)
  Feed GPS bytes → TinyGPS++, update hasFix / lastLat / lastLng
  readMPU() → apply sim override → updateOrientation(dt)
  Compute A_m = sqrt(ax²+ay²+az²)
  pushEnvSample(A_m, roll, pitch)
  Run Fall-Impact-Stillness FSM (if armed)
  Run Skid detector (if armed, FSM idle)
  Run Direct-Impact detector (if armed, FSM idle, not in skid)
  Environmental analysis (every 1 s, if armed, not sim)
  Drive SOS pattern (non-blocking)
```

---

# 4. SENSOR PROCESSING

## 4.1 MPU DATA FLOW

```
Raw MPU Data → Bias Removal → Physical Units → Orientation Estimation
```

### readMPU()

* Reads 14 bytes from register 0x3B
* Extracts ax, ay, az (±2 g range → /16384) and gx, gy, gz (±250 °/s → /131)
* Subtracts calibration offsets

### calibrateMPU()

* 200 samples over CALIB_MS (3000 ms)
* Averages each axis; subtracts 1.0 g from az bias (gravity)
* Stores: aBiasX/Y/Z, gBiasX/Y/Z

### updateOrientation(dt)

Complementary filter (α = 0.02):

```
aRoll  = atan2(ay, az) × 57.296°
aPitch = atan2(-ax, √(ay²+az²)) × 57.296°

roll  = (1-α)×(roll  + gx×dt) + α×aRoll
pitch = (1-α)×(pitch + gy×dt) + α×aPitch
```

## 4.2 ACCELERATION MAGNITUDE

```
A_m = sqrt(ax² + ay² + az²)
```

Orientation-independent scalar used by all detection stages.

---

# 5. FALL DETECTION STATE MACHINE

## States

| State           | Meaning                           |
|-----------------|-----------------------------------|
| FS_IDLE         | Normal monitoring                 |
| FS_FREEFALL     | Low-gravity condition detected    |
| FS_WAIT_IMPACT  | Waiting for impact spike          |
| FS_STILLNESS    | Verifying post-impact unconscious |
| FS_CONFIRMED    | Alert triggered                   |

## Full Flowchart

```
FS_IDLE
  IF A_m < 0.40 g → FS_FREEFALL, start timer

FS_FREEFALL
  IF A_m ≥ 0.40 g → back to FS_IDLE (false trigger)
  IF duration ≥ 100 ms → FS_WAIT_IMPACT
    Record fallStartMs, increment fallEventCount

FS_WAIT_IMPACT
  IF A_m > 3.00 g → FS_STILLNESS, reset stillness buffer
  IF timeout > 500 ms → FS_IDLE, increment fallCancelCount

FS_STILLNESS
  Collect samples for 2000 ms
  Compute σ(A_m)
  IF σ < 0.15 g → FS_CONFIRMED
  ELSE          → FS_IDLE (user conscious), increment fallCancelCount

FS_CONFIRMED
  Increment fallConfirmCount
  Log response time (now − fallStartMs)
  IF !simMode → nrfSendCoords(lastLat, lastLng, "Fall confirmed")
  ELSE        → nrfSendTest(99, "SIMFALL")
  startSOSAlert()
  → FS_IDLE
```

## Why This Works (Thesis Logic)

* Freefall alone → jumping false positive
* Impact alone → dropping the device
* Stillness alone → user sleeping
* **Combination of all three = confirmed real accident**

---

# 6. SECONDARY DETECTORS

## 6.1 Skid / Slide Detector

Active when: `armed == true` AND `fallState == FS_IDLE`

```
Condition: fabsf(ay) > 2.0 g  AND  fabsf(gx) > 330 °/s

If condition holds:
  Start skid timer on first detection
  If sustained ≥ 200 ms:
    skidCount++
    nrfSendCoords() OR nrfSendTest (sim)
    startSOSAlert()
    inSkid = false (reset)

If condition breaks before 200 ms:
  inSkid = false (cancel)
```

Reference: motorcycle crash paper, lateral 0.70 g + roll rate criteria, 96% accuracy.
*(Note: firmware thresholds are set higher — 2.0 g / 330 °/s — for reduced false positives in the specific deployment scenario.)*

---

## 6.2 Direct Impact Detector

Active when: `armed == true` AND `fallState == FS_IDLE` AND `!inSkid`

```
Condition: A_m > 4.50 g  AND  (now − lastImpactAlert) > 5000 ms

  lastImpactAlert = now
  impactCount++
  nrfSendCoords() OR nrfSendTest (sim)
  startSOSAlert()
```

Reference: Sensors 2022 — low-floor fall studies, 4–9 g impact range.

---

## 6.3 Environmental / Structural Instability

Runs every 1000 ms using the last `bufCount` samples from the rolling buffer (max 50).

```
sMag   = stddev(magBuf)
sRoll  = stddev(rollBuf)
sPitch = stddev(pitchBuf)

groundShock:  sMag ≥ 0.40  AND  sRoll < 10.0  AND  sPitch < 10.0
waveMotion:   sMag ≥ 0.24  AND  (sRoll ≥ 10.0 OR sPitch ≥ 10.0)

IF groundShock → nrfSendCoords("Ground shock")   + startSOSAlert()
IF waveMotion  → nrfSendCoords("Wave motion")     + startSOSAlert()
```

*(Environmental analysis is suppressed during simulation mode.)*

---

# 7. PHYSICAL BUTTON – TWO-STATE TOGGLE (v4.1)

**Pin:** GPIO 0 (BOOT button). Active-low with INPUT_PULLUP. 50 ms debounce on falling edge.

```
State A — alertActive = true:
  Press → armed = false
         stopSOSAlert()
         nrfSendFalseAlarm()   ← broadcasts "FA,TOKEN,LAT,LNG"
         lastEvent = "PHYSICAL DISARM – false alarm NRF sent/failed"

State B — armed = false (device was disarmed):
  Press → armed = true
         fallState = FS_IDLE
         inSkid = false
         lastEvent = "Re-armed via physical button"

Ignored — armed = true, alertActive = false:
  (Nothing to cancel — press is silently ignored)
```

---

# 8. NRF COMMUNICATION

## 8.1 Payload Formats (v4.1)

| Type           | Format                        | Example                              |
|----------------|-------------------------------|--------------------------------------|
| EMERGENCY      | `TOKEN,LAT,LNG`               | `DM-001,14.599512,120.984222`        |
| FALSE ALARM    | `FA,TOKEN,LAT,LNG`            | `FA,DM-001,14.5995,120.9842`         |
| TEST (burst/sim)| `TEST,NN,<tag>`              | `TEST,01,SIMFALL`                    |

All payloads are 32 bytes (NRF_PAYLOAD). Lat/Lng precision is 6 decimal places for EMERGENCY, 4 decimal places for FALSE ALARM (to fit within the 32-byte limit alongside the `FA,` prefix and token).

**Known hardware constraint:** IMU readings (ax, ay, gx) are NOT included in the FALSE ALARM payload. The 32-byte capacity is fully occupied by token + GPS coordinates. The Netlify portal displays "—" for IMU fields on the false alarm page and explicitly notes this constraint.

---

## 8.2 NRF Send Functions

### nrfSendCoords(lat, lng, reason)
* Formats `TOKEN,LAT,LNG` into a 32-byte buffer
* Calls `radio.write()`
* Updates `lastEvent`

### nrfSendFalseAlarm()
* Formats `FA,TOKEN,LAT,LNG` (4-decimal precision)
* Calls `radio.write()`
* Returns bool success

### nrfSendTest(seq, tag)
* Formats `TEST,NN,<tag>` (used by burst test and simulation drills)
* Prevents real emergency alerts during drill mode

---

## 8.3 NRF Hardware Config

| Parameter    | Value                  |
|--------------|------------------------|
| CE Pin       | GPIO 2                 |
| CSN Pin      | GPIO 15                |
| MOSI/MISO/SCK| 23 / 19 / 18           |
| Address      | 0xE8E8F0F01LL          |
| Payload size | 32 bytes               |
| PA Level     | RF24_PA_HIGH           |
| Mode         | TX (transmitter only)  |

---

# 9. WEB SERVER + DASHBOARD (ESP32)

## 9.1 Endpoints

| Route                  | Function                                              |
|------------------------|-------------------------------------------------------|
| `/`                    | Serves full dashboard HTML (three tabs)               |
| `/status.json`         | Live sensor data (armed, alertActive, GPS, IMU, fall state, sim) |
| `/diag-stats.json`     | GPS fix stats, fall event counters, burst state       |
| `/burst-results.json`  | 20-shot burst test results per packet                 |
| `/arm`                 | Set armed = true                                      |
| `/disarm`              | Set armed = false, stop SOS; if alertActive → send FA NRF |
| `/false-alarm`         | Manual FA: only works when alertActive; sends FA NRF  |
| `/test-nrf`            | Single legacy NRF test packet                         |
| `/run-burst`           | Start 20-packet burst test (non-blocking)             |
| `/simulate?event=X`    | Start simulation (skid / fall / impact)               |
| `/sim-stop`            | Stop simulation, reset IMU to idle state              |

## 9.2 /status.json Fields

```json
{
  "armed":       true,
  "alertActive": false,
  "hasFix":      true,
  "lat":         14.599512,
  "lng":         120.984222,
  "roll":        2.30,
  "pitch":      -1.20,
  "aMag":        1.023,
  "ay":          0.012,
  "gx":          0.5,
  "nrfReady":    true,
  "fallState":   0,
  "simEvent":    0,
  "inSkid":      false,
  "lastEvent":   "System boot"
}
```

## 9.3 Dashboard Tabs

### Monitor Tab
* **Alert Active Banner** — animated red banner shown when `alertActive = true`; includes hint about physical button and False Alarm button
* **System Controls** — Arm, Disarm, False Alarm (disabled when no active alert), NRF Single Test
* **Live Sensor Data** — 11 KV tiles: Armed, Alert Active, GPS Fix, Lat, Lng, Roll, Pitch, |A|, A-Y lateral, GX roll rate, NRF24 status
* **Fall Pipeline Bar** — color-coded progress bar across 5 FSM stages
* **GPS Location Actions** (v4.1 new):
  * **Share Location** button — copies `https://www.google.com/maps?q=LAT,LNG` to clipboard; disabled until valid GPS fix
  * **Open in Maps** button — opens Google Maps pin in new browser tab; disabled until valid GPS fix
  * GPS coordinate display below buttons (green when fix acquired)
* **Last Event** — monospace event log display
* **Detection Thresholds Reference** — literature-backed threshold summary with physical button behavior note

### Diagnostics Tab
* **NRF Burst Test** — 20-shot burst with per-packet ACK/FAIL status, latency bar, summary stats (success rate, avg/min/max delay)
* **GPS Fix Statistics** — fix count, average time-to-fix, current fix status
* **Fall Detection Diagnostics** — events detected, confirmed alerts, cancelled events, avg response time, precision rate, skid count, direct impact count

### Simulation Tab
* **Event Buttons** — Simulate Skid (SIM_SKID), Simulate Fall (SIM_FALL), Simulate Impact (SIM_IMPACT), Stop Sim
* **Sim Status Display** — live status string from `lastEvent`
* **IMU Threshold Reference Table** — literature-backed thresholds with event name, value, condition, and source
* **Live IMU Waveform Canvas** — rolling 80-sample aMag plot with threshold lines (freefall 0.40 g, skid 0.70 g, impact 3.00 g, direct 4.00 g)

---

# 10. IMU SIMULATION MODE

Three simulation events inject synthetic IMU waveforms to test the detection pipeline end-to-end without physically triggering the device.

### SIM_SKID (~700 ms)
* Sine-enveloped lateral ay peak ~0.85 g + gx peak ~55 °/s
* Designed to cross SKID_LATERAL_G and SKID_ROLL_RATE thresholds
* Sends `nrfSendTest(98, "SIMSKID")` instead of real alert

### SIM_FALL (~2650 ms)
```
0–100 ms  : Pre-fall lean (az drops, gx rises)
100–280 ms: Freefall (A_m ≈ 0.18 g — well below 0.40 g threshold)
280–330 ms: Impact spike (A_m ≈ 4.8 g — above 3.00 g threshold)
330–2650 ms: Stillness (σ < 0.05 g — below 0.15 g threshold)
```
* Sends `nrfSendTest(99, "SIMFALL")`

### SIM_IMPACT (~200 ms)
* Gaussian impulse: peak A_m ≈ 4.8 g (above IMPACT_DIRECT_G = 4.50 g)
* Sends `nrfSendTest(97, "SIMIMPACT")`

All simulations set `lastEvent` with drill status and suppress real NRF emergency packets.

---

# 11. RECEIVER (PRO MICRO) – v2.3

## 11.1 Three Payload Types

```
WAIT for NRF packet (radio.available())
   ↓
Read 32-byte payload → String raw
   ↓
Detect type:
  raw.startsWith("TEST,")  → TEST handler
  raw.startsWith("FA,")    → FALSE ALARM handler
  else                     → EMERGENCY handler
```

## 11.2 EMERGENCY Handler

```
Parse: TOKEN = raw[0..c1], COORDS = raw[c1+1..]
Build URL: https://danger-monitor-profile.netlify.app/?token=TOKEN&loc=LAT,LNG
Debounce: 1500 ms between triggers
openURL(url) → HID keyboard simulation
```

## 11.3 FALSE ALARM Handler (v2.3 New)

```
Parse: FA | TOKEN | LAT | LNG
Build URL: https://danger-monitor-profile.netlify.app/?falarm=1&token=TOKEN&loc=LAT,LNG
Debounce: 1500 ms
openURL(url) → HID keyboard simulation
```
→ Netlify displays **green "USER IS SAFE" page** instead of emergency profile.

## 11.4 TEST Handler

```
Parse seq number from "TEST,NN,tag"
Build URL: https://danger-monitor-profile.netlify.app/?nrftest=1&seq=NN&ts=millis
Debounce: 1500 ms
openURL(url)
```
→ Netlify shows a toast notification and stays on home page.

## 11.5 openURL() – HID Sequence

```
Keyboard.press(Ctrl + L)   → focus address bar
Keyboard.releaseAll()
delay(250)
Keyboard.press(Ctrl + A)   → select existing URL
Keyboard.releaseAll()
delay(80)
Keyboard.print(url)        → type new URL
delay(80)
Keyboard.write(KEY_RETURN) → navigate
```

## 11.6 Wiring

| NRF24 Pin | Pro Micro Pin |
|-----------|---------------|
| CE        | 9             |
| CSN       | 10            |
| MOSI      | 16 (MOSI)     |
| MISO      | 14 (MISO)     |
| SCK       | 15 (SCK)      |
| VCC       | 3.3 V only    |

---

# 12. NETLIFY EMERGENCY PORTAL (Web Layer)

## 12.1 Technology Stack

```
HTML5 / CSS3 / Vanilla JavaScript
QRCode.js (CDN: cdnjs.cloudflare.com)
localStorage (Confirmed Accident Database)
Web Audio API (SOS sound engine)
No backend required — fully static
```

## 12.2 URL Router

The portal reads URL parameters on load and routes to the correct page:

| URL Pattern                                          | Page Shown              |
|------------------------------------------------------|-------------------------|
| `/` (no params)                                      | Home – Device Grid      |
| `/?token=DM-001&loc=14.599,120.984`                  | Emergency Profile       |
| `/?falarm=1&token=DM-001&loc=14.5995,120.9842`       | False Alarm – Safe Page |
| `/?nrftest=1&seq=NN&ts=...`                          | Home + toast            |
| `/?loc=14.599,120.984` (no token)                    | First profile (legacy)  |
| `/?token=DM-XXX` (unknown token)                     | Token Not Found page    |

## 12.3 Pages

### Home Page
* Hero section: system description, stats (device count, repeater spacing, RF protocol)
* **Device Grid** — card per profile showing avatar, name, role, token, blood type, contact
* Clicking any card opens that profile's Emergency Profile view

### Emergency Profile Page
* **Alert Banner** — animated red pulsing banner with SOS mute toggle button
* **Audio Unlock Bar** — shown on mobile when autoplay is blocked; tap to enable SOS alarm
* **Medical Profile Card** — avatar, name, role, token, blood type, allergies, emergency contact, medical notes
* **QR Code Section** — auto-generated `tel:` QR code for instant emergency contact dial
* **Location Card** — live coordinates, Google Maps button (hidden if no location)
* **Rescuer Action Row**:
  * **Share Alert** — copies current emergency URL to clipboard
  * **Confirm Accident** — opens token-confirmation modal; logs entry to Confirmed Accident Database
* Back button → Home

### False Alarm Page
* **Safe Banner** — green pulsing "USER IS SAFE" banner
* **Safe Status Row** — green indicator dot + "DEVICE OWNER CONFIRMED SAFE" text
* **Profile Card (green border)** — same medical info as emergency profile
* **IMU Readings at Disarm** — displays "—" for all three fields with hardware constraint note
* **Location Card** — last-known GPS position

### Confirmed Accident Database Page
* Header with record count and Clear All button
* Per-entry cards showing: avatar, name, token, confirmed timestamp, coordinates (Maps link), medical summary
* **Medical Diagnosis Notes** — collapsible section per accident:
  * Add Text Note (textarea)
  * Add Image (file input → base64)
  * Attach File (file input → base64 data URL)
  * Delete individual notes
* **Delete Accident Record** — per-card ✕ button; requires token confirmation via modal
* Empty state placeholder when no records

### Token Not Found Page
* Shows the unrecognized token string
* Back button → Home

## 12.4 Token Confirmation Modal

Used for both "Confirm Accident" and "Delete Record" actions.

```
openModal({
  mode:  'confirm' | 'delete',
  token: required token string,
  onOk:  callback on success
})
```

* User must type the device token exactly (case-insensitive) to unlock the OK button
* Input highlights green on match; button label differs by mode ("Log to Database" vs "Delete")
* Dismiss via Cancel button, backdrop click, or Escape key

## 12.5 QR Code System

```javascript
generateQRCode(phoneNumber, containerId)
  telLink = 'tel:' + phoneNumber.replace(/\s/g, '')
  new QRCode(container, {
    text:         telLink,
    width:        200, height: 200,
    colorDark:    '#000000',
    colorLight:   '#ffffff',
    correctLevel: QRCode.CorrectLevel.H   // 30% error recovery
  })
```

| Property        | Value                  |
|-----------------|------------------------|
| Encoding        | `tel:` URI protocol    |
| Size            | 200 × 200 px           |
| Error Correction| Level H (30% recovery) |
| Generation      | Client-side (instant)  |
| Offline support | Yes (no network needed)|

## 12.6 SOS Audio Engine

Non-blocking Morse SOS pattern using Web Audio API:

```
Pattern: ... --- ...  (dot=80ms, dash=240ms, gap=80ms, long gap=240ms, word gap=560ms)
Tone:    880 Hz sine wave, gain 0.35
```

* `SOS.tryAutoplay()` — called on profile load; shows unlock bar if browser blocks autoplay
* `SOS.start()` / `SOS.stop()` — start/stop the oscillator pattern loop
* `SOS.toggleMute()` — mute/unmute without stopping the pattern timer
* SOS is always stopped when navigating to False Alarm page or Home

## 12.7 Confirmed Accident Database (localStorage)

```
Key: 'dm_accidents'
Schema per entry:
{
  id:          string (timestamp),
  confirmedAt: ISO date string,
  token:       "DM-XXX",
  profile:     { ...snapshot of profile at time of confirmation },
  loc:         "LAT,LNG" or "",
  url:         full emergency URL,
  diagNotes:   [
    {
      id:       string,
      addedAt:  ISO date string,
      type:     "text" | "image" | "file",
      content:  text string | base64 data URL,
      fileName: string,
      mimeType: string
    }
  ]
}
```

Entries are stored newest-first. Image and file notes are stored as base64 data URLs. A 2 MB per-file warning prompt is shown to prevent exceeding localStorage limits (~5–10 MB per origin).

## 12.8 Registered Profiles

```javascript
const PROFILES = [
  { token: "DM-001", name: "Jan Clyde Talosig",     blood: "B+",  allergies: "None",          ... },
  { token: "DM-002", name: "James Laurence Guzman", blood: "O+",  allergies: "Skin Allergies", ... },
  { token: "DM-003", name: "Kaye Angelique Aquino", blood: "A+",  allergies: "None",           ... },
  { token: "DM-004", name: "Shamel Alliyah Senson", blood: "AB+", allergies: "None",           ... },
  { token: "DM-005", name: "Zarina Marie Aguilar",  blood: "AB+", allergies: "None",
    notes: "Have history of heart complications"                                                  },
];
```

To add a new device, append a new object to the `PROFILES` array in `emergency_portal_qr.html`.

---

# 13. SOS ALERT SYSTEM

## Pattern

Morse Code SOS: `... --- ...` (non-blocking)

```javascript
const SOS_STEPS = [
  {on:true,ms:150},{on:false,ms:150},  // S dot 1
  {on:true,ms:150},{on:false,ms:150},  // S dot 2
  {on:true,ms:150},{on:false,ms:150},  // S dot 3
  {on:true,ms:450},{on:false,ms:150},  // O dash 1
  {on:true,ms:450},{on:false,ms:150},  // O dash 2
  {on:true,ms:450},{on:false,ms:150},  // O dash 3
  {on:true,ms:150},{on:false,ms:150},  // S dot 1
  {on:true,ms:150},{on:false,ms:150},  // S dot 2
  {on:true,ms:150},{on:false,ms:600},  // S dot 3 + word gap
];
```

* LED (GPIO 4) and Buzzer (GPIO 5, 3000 Hz) toggled in sync
* `startSOSAlert()` — sets `alertActive = true`, starts pattern
* `stopSOSAlert()` — sets `alertActive = false`, turns off LED and buzzer
* Pattern cycles with `millis()` timing — does not block the main loop

---

# 14. GPS HANDLING

## Strategy: Last-Known Location

```
IF GPS valid:
    update lastLat, lastLng
    IF first fix after search: record fix time, increment gpsFixCount
ELSE:
    keep last valid coordinates (lastLat, lastLng)

IF fix age > 8000 ms:
    hasFix = false → start searching
```

## GPS Diagnostics

Tracked in firmware and exposed via `/diag-stats.json`:
* `gpsFixCount` — number of times a fix was (re-)acquired
* `gpsTotalFixMs` — cumulative time-to-fix; divide by count for average
* `gpsSearchStart` — timestamp of last fix-lost event

---

# 15. SYSTEM STATES

## Armed Mode (`armed = true`)

* All detection FSMs active (fall, skid, direct impact, environmental)
* SOS alerts allowed
* NRF broadcasting enabled
* Physical button in State A/Ignored logic

## Disarmed Mode (`armed = false`)

* All FSMs reset to idle
* SOS stopped
* NRF dormant
* Physical button in State B (re-arm) logic

## Alert Active (`alertActive = true`)

* Subset of armed mode — a confirmed event is in progress
* SOS LED/buzzer cycling
* Web dashboard shows animated red banner
* False Alarm button enabled
* Physical button triggers disarm + FA NRF

---

# 16. KEY VARIABLES YOU CAN MODIFY

## Detection Thresholds

```cpp
// Fall-Impact-Stillness
FREEFALL_G        = 0.40f    // g — below this triggers freefall
IMPACT_G          = 3.00f    // g — above this after freefall = impact
STILLNESS_STD_MAX = 0.15f    // g σ — below this = unconscious
FREEFALL_DUR_MS   = 100      // ms — min freefall duration
IMPACT_WINDOW_MS  = 500      // ms — impact must occur within this window
STILLNESS_DUR_MS  = 2000     // ms — stillness observation window

// Skid
SKID_LATERAL_G    = 2.0f     // g — lateral ay threshold
SKID_ROLL_RATE    = 330.0f   // °/s — gx roll rate threshold
SKID_DUR_MS       = 200      // ms — sustained duration

// Direct Impact
IMPACT_DIRECT_G   = 4.50f    // g — spike threshold
IMPACT_DEBOUNCE   = 5000     // ms — minimum time between direct impact alerts

// Environmental
ACC_STD_DANGER    = 0.40f    // g σ — acceleration variance threshold
ROLL_PITCH_STD    = 10.0f    // ° σ — orientation variance threshold
ANALYZE_EVERY_MS  = 1000     // ms — analysis interval
```

## Hardware Pins

```cpp
GPS_RX = 16, GPS_TX = 17, GPS_BAUD = 9600
MPU_ADDR = 0x68 (I2C, SDA=21, SCL=22)
NRF_CE = 2, NRF_CSN = 15
ALERT_LED_PIN = 4
BUZZER_PIN = 5
DISARM_BTN_PIN = 0
```

## Device Token

```cpp
const char* DEVICE_TOKEN = "DM-001";
```

Each physical device must have a unique token matching an entry in the `PROFILES` array.

---

# 17. FULL SYSTEM ARCHITECTURE

## v4.1 Block Diagram

```
[MPU6050] ──┐
            ├─→ ESP32 ─→ FALL / SKID / IMPACT FSMs ─→ NRF24 ──┐
[GPS]    ───┘      ↓              ↓                            │
                SOS ALERT   Physical BTN (GPIO 0)              │
               (LED/Buzz)    ↓ (if alertActive)                │
                         FA NRF packet ──────────────────────  │
                                                               ↓
                                                          Pro Micro
                                                               ↓
                                                 HID Keyboard Emulation
                                                               ↓
                                              Browser → Netlify Portal
                                                               ↓
                                         ┌──────────────────────────────┐
                                         │  Emergency Profile + QR      │
                                         │  OR False Alarm Safe Page    │
                                         │  OR Test Toast Notification  │
                                         └──────────────────────────────┘
                                                               ↓
                                                  Responder Scans QR
                                                               ↓
                                                   Phone Auto-Dials
                                                               ↓
                                           (optional) Confirm Accident
                                                               ↓
                                              Accident Database Entry
                                                               ↓
                                          Medical Personnel Adds Notes
```

---

## Data Flow – Emergency Scenario

```
TRANSMITTER (ESP32):
  Fall / Skid / Impact Detected
       ↓
  GPS: lastLat, lastLng
       ↓
  Payload: "DM-001,14.599512,120.984222"
       ↓
  NRF24 → radio.write()
       ↓
  SOS LED + Buzzer activated

RECEIVER (Pro Micro):
  radio.available() → radio.read()
       ↓
  Payload type: EMERGENCY (no "FA," prefix, no "TEST," prefix)
       ↓
  Parse: token="DM-001", coords="14.599512,120.984222"
       ↓
  URL: https://danger-monitor-profile.netlify.app/?token=DM-001&loc=14.599512,120.984222
       ↓
  HID: Ctrl+L → Ctrl+A → type URL → Enter

NETLIFY PORTAL:
  route() reads params
       ↓
  PROFILES.find(token === "DM-001")
       ↓
  renderProfile(profile, "14.599512,120.984222")
       ↓
  Display: photo, name, blood type, allergies, contact, notes
  Generate: QR code (tel:+63917XXXXXX)
  Show:     Google Maps link
  Play:     SOS audio (880 Hz Morse pattern)
       ↓
  Responder scans QR → phone dials contact
  (optional) Rescuer clicks "Confirm Accident" → enters token → logs to DB
```

---

## Data Flow – False Alarm Scenario

```
TRANSMITTER (ESP32):
  User presses GPIO 0 while alertActive = true
       ↓
  stopSOSAlert() — LED/buzzer off
       ↓
  nrfSendFalseAlarm()
  Payload: "FA,DM-001,14.5995,120.9842"
       ↓
  armed = false

RECEIVER (Pro Micro):
  raw.startsWith("FA,") → FALSE ALARM handler
       ↓
  Parse: token="DM-001", loc="14.5995,120.9842"
       ↓
  URL: https://danger-monitor-profile.netlify.app/?falarm=1&token=DM-001&loc=14.5995,120.9842
       ↓
  HID: openURL()

NETLIFY PORTAL:
  route() detects falarm=1
       ↓
  renderFalseAlarm(profile, loc)
       ↓
  Green "USER IS SAFE" banner + profile info
  SOS audio stopped
  IMU fields show "—" (hardware constraint)
  Last known GPS position displayed
```

---

# 18. DEPLOYMENT CHECKLIST (v4.1)

## Transmitter Setup

- [ ] Program ESP32 with v4.1 firmware
- [ ] Set `DEVICE_TOKEN` to match Netlify `PROFILES` entry (e.g., `"DM-001"`)
- [ ] Configure WiFi AP SSID/password in `AP_SSID` / `AP_PASS`
- [ ] Set NRF24 channel and address to match receiver
- [ ] Calibrate MPU-6050 (hold device still during 3 s boot calibration)
- [ ] Test GPS fix (outdoor location, wait for `hasFix = true`)
- [ ] Verify SOS alert (LED + buzzer)
- [ ] Test physical button: arm → trigger alert → press button → verify FA NRF + disarm → press again → verify re-arm

## Receiver Setup

- [ ] Program Pro Micro with v2.3 firmware
- [ ] Configure NRF24 CE=9, CSN=10 to match transmitter address
- [ ] Test NRF pairing (LED blink on packet reception)
- [ ] Connect to PC via USB (HID driver auto-installs)
- [ ] Test keyboard emulation with a test payload

## Web Portal Setup

- [ ] Update `PROFILES` array with real names, photos, and contact numbers
- [ ] Deploy `emergency_portal_qr.html` to Netlify
- [ ] Test emergency URL: `?token=DM-001&loc=14.599,120.984`
- [ ] Test false alarm URL: `?falarm=1&token=DM-001&loc=14.5995,120.9842`
- [ ] Test NRF test URL: `?nrftest=1&seq=01&ts=12345`
- [ ] Verify QR code generation and mobile scan-to-dial
- [ ] Test Confirm Accident flow → Database entry → Diagnosis notes
- [ ] Test Delete Record flow with token confirmation
- [ ] Verify SOS audio on mobile (tap unlock bar if needed)

## Field Testing

- [ ] Controlled fall drill: use Simulate Fall on dashboard, verify FSM progression
- [ ] Skid drill: use Simulate Skid, verify NRF test packet sent
- [ ] Impact drill: use Simulate Impact, verify direct-impact alert
- [ ] Physical button false alarm test (step-by-step toggle)
- [ ] Web disarm false alarm test (dashboard button)
- [ ] NRF 20-shot burst test (check success rate ≥ 90%)
- [ ] GPS Share Location — copy link, verify coordinates
- [ ] GPS Open in Maps — verify correct pin location
- [ ] End-to-end: fall → NRF → browser opens profile → QR scan → call

---

# 19. VERSION COMPARISON

| Feature                        | v2.1        | v4.0           | v4.1                    |
|--------------------------------|-------------|----------------|-------------------------|
| Fall detection                 | FSM (3-stage)| FSM (3-stage) | FSM (3-stage)           |
| Skid detection                 | ✗           | ✓              | ✓                       |
| Direct impact detection        | ✗           | ✓              | ✓                       |
| Simulation drills              | ✗           | ✓              | ✓                       |
| NRF burst test                 | ✗           | ✓              | ✓                       |
| Physical disarm button         | ✗           | Single-press   | Two-state toggle        |
| Physical re-arm                | ✗           | ✗              | ✓ (second press)        |
| False alarm NRF packet         | ✗           | ✓              | ✓                       |
| False alarm Netlify page       | ✗           | ✗              | ✓ (green safe page)     |
| GPS Share Location button      | ✗           | ✗              | ✓                       |
| GPS Open in Maps button        | ✗           | ✗              | ✓                       |
| Multi-profile support          | Token-based | Token-based    | Token-based (5 profiles)|
| QR code quick-dial             | ✓           | ✓              | ✓                       |
| Confirmed accident database    | ✗           | ✗              | ✓ (localStorage)        |
| Medical diagnosis notes        | ✗           | ✗              | ✓ (text/image/file)     |
| SOS audio (browser)            | ✗           | ✗              | ✓ (Web Audio API)       |
| Alert Active indicator         | ✗           | ✓              | ✓                       |
| Dashboard tabs                 | 1           | 3              | 3                       |
| Receiver payload types         | 1           | 2              | 3 (EM + TEST + FA)      |

---

# 20. FINAL NOTES (v4.1)

✔ Non-blocking design throughout (SOS, burst test, button debounce, GPS feed)
✔ Multi-layer detection: Fall FSM + Skid + Direct Impact + Environmental
✔ Two-state physical button toggle: disarm+FA when alert active, re-arm when disarmed
✔ GPS Share and Open in Maps for rapid responder location relay
✔ Token-based routing with 5 registered device profiles
✔ Zero-installation responder workflow (browser-based Netlify portal)
✔ False alarm broadcast prevents unnecessary emergency response
✔ Confirmed accident database with full medical diagnosis notes system
✔ SOS audio engine with mobile autoplay unlock handling
✔ IMU simulation drills with realistic waveforms for pre-deployment testing
✔ Thesis-grade documentation with literature-backed detection thresholds

---

**Last Updated:** April 2026
**Firmware Versions:** ESP32 v4.1 | Pro Micro Receiver v2.3 | Netlify Portal v4.x
**Status:** Production Ready
