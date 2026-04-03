# SHM SYSTEM – FULL CODE DOCUMENTATION (LLM DESCRIPTION)

### Structural Health Monitoring Firmware v2 (ESP32-S3) - By: Jan Clyde T. Talosig

---

# 1. SYSTEM OVERVIEW

## 1.1 Purpose

This system is a **real-time Structural Health Monitoring (SHM) platform** that:

* Reads vibration data from **3 MPU6050 sensors (X, Y, Z axes)**
* Processes signals using **RMS + FFT**
* Visualizes data in a **3D heatmap dashboard**
* Logs all data into **CSV (LittleFS)**
* Detects anomalies (vibration-based)

---

## 1.2 System Architecture

```
MPU6050 (3x) → ESP32-S3 → FreeRTOS Tasks → Processing → Web UI + CSV Logging
```

---

# 2. ABSTRACT PROGRAM FLOW (SUMMARY)

## MASTER FLOW

```
BOOT
  ↓
INITIALIZE SYSTEM
  ↓
START TASKS (FreeRTOS)
  ↓
[LOOP RUNS FOREVER]
    ├─ Sensor Acquisition Task (Core 0)
    ├─ Signal Processing Task (Core 1)
    ├─ Web Server Task (Core 1)
    ↓
UPDATE UI + LOG DATA + DETECT ANOMALIES
```

---

## REAL-TIME PIPELINE

```
Sensor Read → Buffer → RMS + FFT → Intensity → JSON → UI + CSV
```

---

# 3. DETAILED FLOWCHART (ULTRA-DETAILED)

```
START

setup():
  Initialize Serial
  Initialize I2C
  Configure AD0 pins (sensor selection)
  Initialize all MPU6050 sensors
  Start WiFi AP
  Start WebServer
  Mount LittleFS
  Open CSV file
  Create Mutexes
  Create FreeRTOS Tasks

loop():
  Sleep (system is task-driven)

--------------------------------------------------------

TASK 1: SENSOR ACQUISITION (Core 0)

LOOP EVERY ~6 ms:
  FOR each sensor (X,Y,Z):
    Select sensor via AD0
    Read MPU6050
    Convert raw → physical values
    Compute acceleration magnitude

    Store in:
      → RMS buffer
      → FFT buffer

    Lock data mutex:
      Update shared struct
    Unlock mutex

    Lock file mutex:
      Append CSV row
      Flush if needed
    Unlock mutex

--------------------------------------------------------

TASK 2: SIGNAL PROCESSING (Core 1)

LOOP EVERY 50 ms:
  FOR each axis:
    IF enough samples:
      Compute RMS
      Compute FFT
      Compute intensity

      Lock data mutex:
        Update shared processed values
      Unlock mutex

--------------------------------------------------------

TASK 3: WEB SERVER (Core 1)

LOOP EVERY 50 ms:
  Handle HTTP requests

--------------------------------------------------------

CLIENT SIDE (BROWSER)

EVERY 50 ms:
  Fetch /data JSON
  Update:
    → 3D heatmap
    → Table
    → HUD stats
    → Anomaly detection

END
```

---

# 4. SENSOR SYSTEM (MPU6050)

---

## 4.1 MULTIPLEXING (CRITICAL DESIGN)

### Problem:

All sensors have SAME I2C address (0x68)

### Solution:

Use AD0 pins to switch addresses

```
selectSensor(i):
  Set sensor[i] → LOW (active)
  Set others → HIGH (inactive)
```

✔ Only ONE sensor active at a time

---

## 4.2 DATA ACQUISITION

### readMPU6050()

Steps:

1. Request 14 bytes from MPU
2. Extract:

   * Acceleration (ax, ay, az)
   * Gyroscope (gx, gy, gz)
3. Convert:

   * accel → g (÷16384)
   * gyro → °/s (÷131)

---

## 4.3 ACCELERATION MAGNITUDE

```
mag = sqrt(ax² + ay² + az²) - 1g
```

✔ Removes gravity
✔ Represents vibration strength

---

# 5. SIGNAL PROCESSING

---

## 5.1 RMS (VIBRATION ENERGY)

```
RMS = sqrt( Σ(x²) / N )
```

### Purpose:

* Measures vibration intensity
* Used for anomaly detection

---

## 5.2 FFT (FREQUENCY ANALYSIS)

```
FFT → dominant frequency
```

Steps:

1. Apply Hamming window
2. Perform FFT
3. Extract peak frequency

---

## 5.3 INTENSITY NORMALIZATION

```
intensity = RMS / MAX_RMS_G
```

Clamped:

```
0 → no vibration
1 → maximum vibration
```

---

# 6. DATA STORAGE SYSTEM (LittleFS)

---

## CSV FORMAT

```
timestamp,axis,ax,ay,az,rms,freq,intensity
```

---

## WRITE LOGIC

```
Every sensor read:
  Append row to file

Flush condition:
  IF bytes > 1024 OR time > 3 sec
```

---

## DOWNLOAD FLOW

```
User clicks download:
  Lock file
  Flush + close file
  Stream file to browser
  Delete file
  Recreate file with header
  Unlock file
```

✔ Memory-efficient (no RAM buffering)
✔ Auto-reset after download

---

# 7. MULTI-TASKING (FreeRTOS DESIGN)

---

## TASK DISTRIBUTION

| Task       | Core   | Purpose               |
| ---------- | ------ | --------------------- |
| Sensor     | Core 0 | Real-time acquisition |
| Processing | Core 1 | DSP calculations      |
| Web        | Core 1 | UI handling           |

---

## SYNCHRONIZATION

### Mutexes:

```
gDataMutex → protects sensor data
gFileMutex → protects file access
```

✔ Prevents race conditions
✔ Ensures thread safety

---

# 8. WEB SERVER API

---

## ROUTES

| Endpoint  | Description |
| --------- | ----------- |
| /         | Dashboard   |
| /data     | JSON data   |
| /download | CSV file    |
| /three.js | 3D engine   |

---

## JSON STRUCTURE

```
{
  "nodes":[
    {
      "axis":"X",
      "acc":[ax,ay,az],
      "gyro":[gx,gy,gz],
      "rms":0.02,
      "freq":12.5,
      "intensity":0.4
    }
  ]
}
```

---

# 9. WEB UI SYSTEM (3D HEATMAP)

---

## VISUAL MODEL

```
3 PLANES:
  Floor → X-axis
  Left Wall → Y-axis
  Back Wall → Z-axis
```

---

## GRID

```
Each plane = 8×8 tiles
Total = 64 tiles per plane
```

---

## INTENSITY PROPAGATION

Gaussian distribution:

```
center = highest intensity
edges = lower intensity
```

---

## ANIMATION

```
current_value += (target - current) * smoothing_factor
```

✔ Smooth transitions
✔ No flickering

---

# 10. ANOMALY DETECTION

---

## THRESHOLDS

```
WARN  = RMS ≥ 0.08g
CRIT  = RMS ≥ 0.15g
```

---

## LOGIC

```
IF RMS ≥ CRIT:
  Show anomaly banner
  Highlight table
  Flash UI
```

---

# 11. DATA FLOW SUMMARY

```
IMU → Raw Data
     ↓
Magnitude Calculation
     ↓
RMS Buffer + FFT Buffer
     ↓
Signal Processing Task
     ↓
Shared Data Struct
     ↓
Web JSON + CSV Logging
     ↓
UI Visualization
```

---

# 12. HOW TO MODIFY SYSTEM

---

## Change Sampling Rate

```
#define SENSOR_PERIOD_MS
```

---

## Change FFT Resolution

```
#define FFT_SAMPLES
```

---

## Change Detection Sensitivity

```
THRESH_WARN
THRESH_CRIT
MAX_RMS_G
```

---

## Change UI

Edit:

```
WebServerUI.h
```

---

## Change Signal Processing

Edit:

```
Calculations.h
```

---

## Change Architecture

Edit:

```
FreeRTOS task structure
```

---

# 13. DESIGN STRENGTHS

✔ Real-time processing (FreeRTOS)
✔ Memory efficient (LittleFS streaming)
✔ Scalable (can expand nodes)
✔ Modular (separate logic layers)
✔ Visualization-driven analysis

---

# 14. FINAL SYSTEM MODEL

```
[SENSORS]
   ↓
[ACQUISITION TASK]
   ↓
[BUFFERS]
   ↓
[PROCESSING TASK]
   ↓
[SHARED MEMORY]
   ↓
 ├── WEB SERVER → DASHBOARD
 └── FILE SYSTEM → CSV LOG
```

---

# END OF DOCUMENT
