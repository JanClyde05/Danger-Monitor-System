# GPS Danger Monitor System – Full Code Documentation (v2.0)

---

# 1. SYSTEM OVERVIEW

## 1.1 Purpose

The system is a **wearable/embedded safety monitor** that detects:

* Human falls (high confidence)
* Environmental hazards (ground shock / instability)

It then:

1. Gets GPS coordinates
2. Sends them via NRF24L01
3. Triggers an SOS alert (LED + buzzer)
4. Opens a web-based emergency profile via HID receiver

---

## 1.2 System Components

### Transmitter (ESP32)

* Sensors:

  * MPU-6050 (motion)
  * GPS (TinyGPS++)
* Communication:

  * NRF24L01 (wireless)
* Interface:

  * Web dashboard (AP mode)

### Receiver (Pro Micro)

* NRF24L01 (receiver)
* HID Keyboard (opens alert URL)

### Web Layer

* Embedded dashboard (ESP32)
* Netlify emergency profile page

---

# 2. ABSTRACT PROGRAM FLOW (SUMMARY)

## HIGH-LEVEL FLOW

```
BOOT → INIT HARDWARE → CALIBRATION → RUN LOOP
         ↓
     READ SENSORS
         ↓
  UPDATE STATE MACHINE
         ↓
  CHECK ENVIRONMENTAL EVENTS
         ↓
  SEND ALERT (NRF + SOS)
         ↓
  UPDATE WEB UI
         ↓
       REPEAT
```

---

## CORE LOGIC PIPELINE

### FALL DETECTION PIPELINE

```
[IDLE]
   ↓ (A_m < 0.4g for 100ms)
[FREEFALL]
   ↓ (A_m > 3g within 500ms)
[IMPACT]
   ↓ (low motion for 2s)
[STILLNESS]
   ↓
[CONFIRMED FALL → ALERT]
```

✔ ALL stages must pass → eliminates false positives

---

# 3. DETAILED MODULE EXPLANATION

---

# 3.1 SETUP PHASE

## Responsibilities

* Initialize all hardware
* Configure communication
* Calibrate sensors

## Steps

```
setup():
  Start Serial
  Start GPS UART
  Initialize I2C (MPU)
  Wake MPU
  Calibrate MPU (3 seconds)
  Start WiFi AP
  Initialize NRF24
  Setup WebServer routes
  Configure GPIO (LED, buzzer)
```

---

# 3.2 MAIN LOOP EXECUTION

## Loop Cycle Structure

```
loop():
  Compute delta time (dt)
  Handle web requests
  Read GPS
  Read IMU
  Compute acceleration magnitude
  Run fall detection FSM
  Run environmental analysis
  Update SOS alert pattern
```

---

# 4. SENSOR PROCESSING

---

## 4.1 MPU DATA FLOW

```
Raw MPU Data → Bias Removal → Physical Units → Orientation Estimation
```

### Functions

#### readMPU()

* Reads 14 bytes from MPU
* Extracts:

  * Acceleration (ax, ay, az)
  * Gyroscope (gx, gy, gz)
* Applies calibration offsets

---

#### calibrateMPU()

* Collects ~200 samples
* Computes average bias
* Adjusts:

  * accel offset
  * gyro drift

---

#### updateOrientation(dt)

* Uses complementary filter:

  * Gyro = fast response
  * Accel = stable reference

```
roll = (1-α)*gyro + α*accel
pitch = (1-α)*gyro + α*accel
```

---

## 4.2 ACCELERATION MAGNITUDE

```
A_m = sqrt(ax² + ay² + az²)
```

✔ Orientation-independent
✔ Used for fall detection

---

# 5. FALL DETECTION STATE MACHINE (CRITICAL)

---

## States

| State          | Meaning                  |
| -------------- | ------------------------ |
| FS_IDLE        | Monitoring               |
| FS_FREEFALL    | Low gravity detected     |
| FS_WAIT_IMPACT | Waiting for spike        |
| FS_STILLNESS   | Checking unconsciousness |
| FS_CONFIRMED   | Alert triggered          |

---

## FULL FLOWCHART (DETAILED)

```
START (FS_IDLE)

IF A_m < 0.4g:
    → FS_FREEFALL
    → Start timer

FS_FREEFALL:
    IF A_m rises early:
        → FALSE TRIGGER → FS_IDLE
    IF duration ≥100ms:
        → FS_WAIT_IMPACT

FS_WAIT_IMPACT:
    IF A_m > 3g:
        → IMPACT DETECTED
        → FS_STILLNESS
    IF timeout >500ms:
        → CANCEL → FS_IDLE

FS_STILLNESS:
    Collect samples for 2 seconds

    Compute standard deviation σ

    IF σ < 0.15:
        → USER NOT MOVING
        → FS_CONFIRMED
    ELSE:
        → USER MOVING → SAFE
        → FS_IDLE

FS_CONFIRMED:
    Send NRF alert
    Activate SOS
    Return to FS_IDLE
```

---

## WHY THIS WORKS (THESIS LOGIC)

* Freefall alone → jumping false positive
* Impact alone → dropping device
* Stillness alone → sleeping

✔ COMBINATION = real accident detection

---

# 6. ENVIRONMENTAL ANALYSIS

---

## Purpose

Detect:

* Structural impacts
* Unstable movement (waves, tumbling)

---

## Method

### Rolling Buffer

* Stores last 50 samples:

  * acceleration magnitude
  * roll
  * pitch

---

## Standard Deviation Calculation

```
σ = sqrt( (Σx²/n) − (Σx/n)² )
```

---

## Detection Logic

### Ground Shock

```
High accel variance
LOW orientation change
```

→ Indicates sudden impact (e.g., drop)

---

### Wave Motion

```
Moderate accel variance
HIGH orientation variation
```

→ Indicates instability

---

## Actions

```
IF groundShock OR waveMotion:
    Send NRF
    Trigger SOS
```

---

# 7. NRF COMMUNICATION

---

## Payload Format

```
"LAT,LNG"
Example:
"14.599512,120.984222"
```

---

## Transmission Flow

```
ESP32:
  Format string
  radio.write()

Receiver:
  radio.read()
  Parse coordinates
  Trigger HID keyboard
```

---

# 8. RECEIVER (PRO MICRO) FLOW

---

## Logic

```
WAIT for NRF data
   ↓
Read payload
   ↓
Validate format
   ↓
Construct URL:
https://danger-monitor-profile.netlify.app/?loc=LAT,LNG
   ↓
Simulate keyboard:
  Ctrl+L
  Ctrl+A
  Type URL
  Press Enter
```

---

## Result

Browser automatically opens emergency page

---

# 9. SOS ALERT SYSTEM

---

## Pattern

Morse Code: SOS

```
... --- ...
```

### Implementation

* Non-blocking timing
* Array of ON/OFF durations

---

## Logic

```
startSOSAlert():
  Initialize pattern index
  Set first state

loop():
  If time reached:
    Move to next step
    Toggle LED + buzzer
```

---

# 10. WEB SERVER + UI

---

## Endpoints

| Route        | Function          |
| ------------ | ----------------- |
| /            | Dashboard UI      |
| /status.json | System data       |
| /arm         | Enable detection  |
| /disarm      | Disable detection |
| /test-nrf    | Manual test       |

---

## Data JSON Example

```
{
  "armed": true,
  "lat": 14.599,
  "lng": 120.984,
  "roll": 2.3,
  "pitch": -1.2,
  "aMag": 1.02,
  "fallState": 2
}
```

---

## UI Behavior

* Refresh every ~900 ms
* Displays:

  * GPS
  * motion
  * system status
  * fall pipeline progress

---

# 11. GPS HANDLING

---

## Strategy: Last-Known Location

```
IF GPS valid:
    update coordinates
ELSE:
    keep last valid position
```

✔ Ensures location is always available
✔ Works in GPS dead zones

---

# 12. SYSTEM STATES

---

## Armed Mode

* All detections active
* Alerts allowed

## Disarmed Mode

* FSM disabled
* Alerts stopped

---

# 13. KEY VARIABLES YOU CAN MODIFY

---

## Detection Sensitivity

```
FREEFALL_G        (default 0.40g)
IMPACT_G          (default 3.0g)
STILLNESS_STD_MAX (default 0.15g)
```

---

## Timing

```
FREEFALL_DUR_MS
IMPACT_WINDOW_MS
STILLNESS_DUR_MS
```

---

## Environmental Detection

```
ACC_STD_DANGER
ROLL_PITCH_STD
```

---

# 14. HOW TO MODIFY SYSTEM BEHAVIOR

---

## Change Detection Logic

Edit:

```
switch(fallState)
```

---

## Change Alert Behavior

Edit:

```
startSOSAlert()
SOS_STEPS[]
```

---

## Change Communication

Edit:

```
nrfSendCoords()
```

---

## Change UI

Edit:

```
web_ui.h
```

---

## Change Receiver Behavior

Edit:

```
Keyboard.print()
```

---

# 15. FULL SYSTEM ARCHITECTURE

```
[MPU6050] ──┐
            ├─→ ESP32 → FALL DETECTION → NRF24 → Pro Micro → Browser
[GPS]    ───┘                        ↓
                                SOS ALERT
                                ↓
                          Web Dashboard (AP)
```

---

# 16. FINAL NOTES

✔ Non-blocking design
✔ Multi-layer detection (robust)
✔ Modular architecture (easy to expand)
✔ Thesis-ready logic (high reliability)

---

# END OF DOCUMENT
