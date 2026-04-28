/*  GPS_Danger_Monitor.ino  –  v3.0  (Diagnostics + Simulation Edition)
 *  ESP32 + TinyGPS++ + MPU-6050 + NRF24L01 + WebServer (AP mode)
 *
 *  ── Detection pipeline (thesis-grade) ──────────────────────────────────
 *  PRIMARY: Fall-Impact-Stillness FSM
 *    1. FREEFALL  – A_m < 0.40 g  for ≥ 100 ms
 *    2. IMPACT    – A_m > 3.00 g  within 500 ms of freefall end
 *    3. STILLNESS – σ(A_m)< 0.15 g over 2 s after impact
 *
 *  SECONDARY: Parallel event detectors
 *    SKID/SLIDE   – lateral ay > 0.70 g  AND  roll-rate gx > 50 °/s
 *                   sustained ≥ 200 ms  (Ref: motorcycle crash paper, 96% acc)
 *    DIRECT IMPACT– A_m spike > 4.00 g  without freefall precondition
 *                   (Ref: construction-site LF falls 4-9g range, Sensors 2022)
 *
 *  ── New in v3.0 ─────────────────────────────────────────────────────────
 *  • 20-shot NRF burst test (non-blocking, 100 ms spacing)
 *  • GPS fix-time tracking (fix count, avg time-to-fix)
 *  • Fall detection stats (response time, false-positive rate, skid/impact counts)
 *  • IMU simulation mode (Skid / Fall / Direct-Impact waveforms)
 *  • New endpoints: /run-burst  /burst-results.json  /diag-stats.json  /simulate
 *
 *  ── Pins ────────────────────────────────────────────────────────────────
 *    GPS  UART1  : RX=16, TX=17 @ 9600
 *    MPU6050 I2C : SDA=21, SCL=22, addr=0x68
 *    NRF24       : MOSI=23, MISO=19, SCK=18, CE=2, CSN=15
 *    LED         : GPIO 4   |   Buzzer : GPIO 5
 *
 *  Libraries: TinyGPSPlus (Mikal Hart), RF24 (TMRh20)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <SPI.h>
#include <RF24.h>
#include <math.h>
#include "web_ui.h"

/* ═══════════════════════ USER CONFIG ═══════════════════════════════════ */
static const int      GPS_RX   = 16;
static const int      GPS_TX   = 17;
static const uint32_t GPS_BAUD = 9600;

const char*  DEVICE_TOKEN = "DM-001";

const char*  AP_SSID    = "GPS Danger Monitor";
const char*  AP_PASS    = "";
IPAddress    apIP       (192,168,4,1);
IPAddress    apGateway  (192,168,4,1);
IPAddress    apSubnet   (255,255,255,0);

/* ── Fall-Impact-Stillness thresholds ── */
const float    FREEFALL_G        = 0.40f;   // g  – below = freefall
const uint32_t FREEFALL_DUR_MS   = 100;     // ms – must persist
const float    IMPACT_G          = 3.00f;   // g  – spike above = impact
const uint32_t IMPACT_WINDOW_MS  = 500;     // ms – window after freefall
const uint32_t STILLNESS_DUR_MS  = 2000;    // ms – observe for stillness
const float    STILLNESS_STD_MAX = 0.15f;   // g  – σ below = person still

/* ── Skid detection thresholds (Ref: Motorcycle IoT crash paper 2023) ── */
const float    SKID_LATERAL_G    = 2.0f;   // g   – lateral ay threshold
const float    SKID_ROLL_RATE    = 330.0f;   // °/s – roll-rate gx threshold
const uint32_t SKID_DUR_MS       = 200;     // ms  – both must persist

/* ── Direct-impact threshold (Ref: Sensors 2022, construction-site falls) */
const float    IMPACT_DIRECT_G   = 4.50f;   // g  – single spike, no freefall
const uint32_t IMPACT_DEBOUNCE   = 5000;    // ms – re-alert dead-time

/* ── Environmental instability (secondary alert) ── */
const float    ACC_STD_DANGER    = 0.40f;
const float    ROLL_PITCH_STD    = 10.0f;
const uint32_t ANALYZE_EVERY_MS  = 1000;
const uint32_t CALIB_MS          = 3000;
/* ══════════════════════════════════════════════════════════════════════ */

/* ── Hardware ── */
TinyGPSPlus    gps;
HardwareSerial gpsSerial(1);
WebServer      server(80);

#define NRF_CE  2
#define NRF_CSN 15
RF24 radio(NRF_CE, NRF_CSN);
const uint64_t NRF_ADDRESS = 0xE8E8F0F01LL;
const uint8_t  NRF_PAYLOAD = 32;

#define ALERT_LED_PIN 4
#define BUZZER_PIN    5

/* ── MPU-6050 ── */
const uint8_t MPU_ADDR = 0x68;
float ax, ay, az, gx, gy, gz;
float aBiasX=0, aBiasY=0, aBiasZ=0;
float gBiasX=0, gBiasY=0, gBiasZ=0;
float roll=0, pitch=0;
const float ALPHA = 0.02f;

/* ── GPS state ── */
float    lastLat=0, lastLng=0;
bool     hasFix=false;

/* ── GPS diagnostics ── */
uint32_t gpsFixCount      = 0;
uint32_t gpsTotalFixMs    = 0;
uint32_t gpsSearchStart   = 0;    // millis when searching started (boot or last lost)

/* ── System state ── */
bool    armed      = true;
String  lastEvent  = "System boot";
uint32_t lastAnalyze = 0;
bool    nrfReady   = false;
bool    streamToSerial = true;

/* ── Rolling statistics buffers ── */
const int BUF_N = 50;
float magBuf[BUF_N], rollBuf[BUF_N], pitchBuf[BUF_N];
int   bufIdx=0, bufCount=0;

/* ── Stillness buffer (post-impact) ── */
const int STILL_BUF_N = 40;
float stillBuf[STILL_BUF_N];
int   stillIdx=0, stillCount=0;

/* ══════════════ Fall State Machine ══════════════════════════════════ */
enum FallState { FS_IDLE, FS_FREEFALL, FS_WAIT_IMPACT, FS_STILLNESS, FS_CONFIRMED };
FallState fallState = FS_IDLE;
uint32_t  fsTimer   = 0;

/* ── Fall diagnostics ── */
uint32_t fallStartMs      = 0;   // when FS_FREEFALL→FS_WAIT_IMPACT transition happened
uint32_t fallEventCount   = 0;   // confirmed freefalls (past 100ms threshold)
uint32_t fallConfirmCount = 0;   // full Freefall+Impact+Stillness confirmations
uint32_t fallCancelCount  = 0;   // events that were canceled (false positives)
uint32_t fallTotalRespMs  = 0;   // sum of response times for avg computation

/* ── Secondary event counters ── */
uint32_t skidCount   = 0;
uint32_t impactCount = 0;

/* ── Skid state ── */
bool     inSkid    = false;
uint32_t skidTimer = 0;

/* ── Direct impact debounce ── */
uint32_t lastImpactAlert = 0;

/* ══════════════ SOS Alert (non-blocking) ════════════════════════════ */
bool alertActive = false;
struct AlertStep { bool on; uint16_t ms; };
const AlertStep SOS_STEPS[] = {
  {true,150},{false,150},{true,150},{false,150},{true,150},{false,150},
  {true,450},{false,150},{true,450},{false,150},{true,450},{false,150},
  {true,150},{false,150},{true,150},{false,150},{true,150},{false,600}
};
const int SOS_N = sizeof(SOS_STEPS)/sizeof(SOS_STEPS[0]);
int      sosIdx        = 0;
uint32_t sosNextChange = 0;

/* ══════════════ NRF Burst Test ══════════════════════════════════════ */
#define BURST_N 20
struct BurstResult { bool ok; uint16_t delayMs; };
BurstResult burstResults[BURST_N];
bool     burstPending = false;
int      burstDone    = -1;      // -1 = never run, ≥0 = packets completed
int      burstCurrent = 0;
uint32_t burstNextT   = 0;

/* ══════════════ IMU Simulation Mode ════════════════════════════════ */
enum SimEvent { SIM_NONE, SIM_SKID, SIM_FALL, SIM_IMPACT };
SimEvent simEvent  = SIM_NONE;
uint32_t simStart  = 0;
bool     simMode   = false;  // when true: suppress real NRF alerts, use test packets

/* ════════════════════════════════════════════════════════════════════
 *  IMU SIMULATION WAVEFORM DEFINITIONS
 *  All values are override targets for global ax/ay/az/gx/gy/gz.
 *  The complementary filter runs normally on these overridden values
 *  so the FSM processes them identically to real sensor data.
 *
 *  Threshold annotations (shown on dashboard waveform):
 *    Freefall line   : aMag = 0.40 g  (orange dashed)
 *    Skid lateral    : ay   = 0.70 g  (yellow dashed)
 *    Impact line     : aMag = 3.00 g  (red dashed)
 *    Direct impact   : aMag = 4.00 g  (red solid)
 * ══════════════════════════════════════════════════════════════════ */
void applySimulation() {
  if (simEvent == SIM_NONE) return;
  uint32_t t = millis() - simStart;

  switch (simEvent) {
    /* ── SKID WAVEFORM ────────────────────────────────────────────
     *  Based on: motorcycle crash paper threshold 0.70g, 96% accuracy
     *  + airbag paper: angular velocity ≥ 30°/s for falling motion
     *
     *  Simulates: person skidding sideways on wet surface.
     *  ay (lateral) rises to 0.85g, gx (roll rate) to 55°/s.
     *  aMag ≈ 1.32g – elevated but below impact threshold (safe).
     */
    case SIM_SKID: {
      float ph = (float)t / 300.0f * 3.14159f;
      float env = (ph < 3.14159f) ? sinf(ph) : 0.0f;
      if (env < 0) env = 0;
      ay = 0.85f * env;            // lateral spike (triggers SKID_LATERAL_G > 0.70g)
      gx = 55.0f * env;            // roll rate    (triggers SKID_ROLL_RATE > 50°/s)
      az = 0.95f;                  // mostly upright
      ax = 0.05f;
      gy = gz = 0.0f;
      if (t >= 700) {
        ax=ay=gz=gx=gy=0; az=1.0f;
        simEvent = SIM_NONE; simMode = false;
        lastEvent = "SIM done: Skid drill complete";
      } else if (env > 0.82f) {
        lastEvent = "SIM: SKID – lateral " + String(ay,2) + "g + roll " + String(gx,0) + "°/s";
      }
      break;
    }

    /* ── FALL WAVEFORM ────────────────────────────────────────────
     *  Based on: SisFall dataset, airbag paper (AR ≤ 0.38g),
     *  construction-site impact 4-9g (Sensors 2022)
     *
     *  Phase 0   (0–100ms): pre-fall lean, aMag drops to ~0.55g
     *  Phase 1 (100–280ms): freefall, aMag ≈ 0.18g, pitch/roll diverge
     *  Phase 2 (280–330ms): IMPACT spike, aMag = 4.8g  
     *  Phase 3 (330–2600ms): stillness, aMag ≈ 0.97g, σ < 0.05g
     *  → FSM detects and fires alert at ~2.6 s
     */
    case SIM_FALL: {
      if (t < 100) {
        // Pre-fall: body leans, some acceleration change
        float r = (float)t / 100.0f;
        ax = 0.0f + 0.35f * r;   ay = 0.0f; az = 1.0f - 0.45f * r;
        gx = 15.0f * r; gy = gz = 0.0f;
        lastEvent = "SIM FALL: pre-fall lean";
      } else if (t < 280) {
        // Freefall: all axes near zero – "weightlessness"
        ax = 0.05f;  ay = 0.04f;  az = 0.12f;   // aMag ≈ 0.18g < 0.40g ✓
        gx = 30.0f;  gy = 5.0f;   gz = 3.0f;
        lastEvent = "SIM FALL: FREEFALL (~0.18g)";
      } else if (t < 330) {
        // IMPACT spike
        ax = -3.0f;  ay = 1.8f;   az = 3.2f;    // aMag ≈ 4.8g > 3.0g ✓
        gx = 120.0f; gy = 80.0f;  gz = 60.0f;
        lastEvent = "SIM FALL: IMPACT SPIKE (~4.8g)";
      } else if (t < 2650) {
        // Stillness – person motionless after impact
        ax = 0.0f;   ay = 0.02f;  az = 0.97f;   // aMag ≈ 0.97g, very quiet
        gx = 0.5f;   gy = 0.3f;   gz = 0.2f;
        if (t > 400) lastEvent = "SIM FALL: stillness (σ < 0.05g)";
      } else {
        // Reset
        ax=0; ay=0; az=1.0f; gx=gy=gz=0;
        simEvent = SIM_NONE; simMode = false;
        lastEvent = "SIM done: Fall drill complete";
      }
      break;
    }

    /* ── DIRECT IMPACT WAVEFORM ────────────────────────────────────
     *  Based on: construction-site falls 4-9g range (Sensors 2022)
     *  Simulates: side collision / hard stop while stationary.
     *  Gaussian pulse: peak 5.2g at t≈40ms, σ=18ms.
     *  No freefall precondition → fires IMPACT_DIRECT_G (4.0g) detector.
     */
    case SIM_IMPACT: {
      float sigma = 18.0f;
      float gauss = expf(-0.5f * powf(((float)t - 40.0f) / sigma, 2.0f));
      ax = -3.2f * gauss;
      ay =  2.0f * gauss;
      az =  1.0f + 2.8f * gauss;   // aMag ≈ 5.2g peak
      gx =  30.0f * gauss;
      gy = gy = gz = 0.0f;
      if (t < 200) {
        if (gauss > 0.5f) lastEvent = "SIM IMPACT: spike " + String(sqrt(ax*ax+ay*ay+az*az),1) + "g";
      } else {
        ax=0; ay=0; az=1.0f; gx=gy=gz=0;
        simEvent = SIM_NONE; simMode = false;
        lastEvent = "SIM done: Impact drill complete";
      }
      break;
    }

    default: break;
  }
}

/* ═══════════════════════ Utilities ═════════════════════════════════ */
void buzzerOn (uint32_t hz){ tone(BUZZER_PIN, hz); }
void buzzerOff(){ noTone(BUZZER_PIN); }

void mpuWrite(uint8_t reg, uint8_t val){
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

void readMPU(){
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom((int)MPU_ADDR, 14, true);

  int16_t rAx=(Wire.read()<<8)|Wire.read();
  int16_t rAy=(Wire.read()<<8)|Wire.read();
  int16_t rAz=(Wire.read()<<8)|Wire.read();
  Wire.read(); Wire.read();
  int16_t rGx=(Wire.read()<<8)|Wire.read();
  int16_t rGy=(Wire.read()<<8)|Wire.read();
  int16_t rGz=(Wire.read()<<8)|Wire.read();

  ax = (rAx/16384.0f) - aBiasX;
  ay = (rAy/16384.0f) - aBiasY;  //calibration formula, since no standard tool, divided by hula hula number
  az = (rAz/16384.0f) - aBiasZ;
  gx = (rGx/131.0f)   - gBiasX;
  gy = (rGy/131.0f)   - gBiasY;
  gz = (rGz/131.0f)   - gBiasZ;
}

void calibrateMPU(){
  aBiasX=aBiasY=aBiasZ=gBiasX=gBiasY=gBiasZ=0;
  const int N=200;
  float sax=0,say=0,saz=0,sgx=0,sgy=0,sgz=0;
  for(int i=0;i<N;i++){
    readMPU();
    sax+=ax; say+=ay; saz+=az;
    sgx+=gx; sgy+=gy; sgz+=gz; //calibratiion process
    delay(CALIB_MS/N);
  }
  aBiasX=sax/N; aBiasY=say/N; aBiasZ=saz/N-1.0f;
  gBiasX=sgx/N; gBiasY=sgy/N; gBiasZ=sgz/N;
}

void updateOrientation(float dt){
  float aRoll  = atan2(ay,az)*57.29578f;
  float aPitch = atan2(-ax,sqrt(ay*ay+az*az))*57.29578f; // roll and pitch calibration
  roll  += gx*dt;  pitch += gy*dt;
  roll  = (1.0f-ALPHA)*roll  + ALPHA*aRoll;
  pitch = (1.0f-ALPHA)*pitch + ALPHA*aPitch;
}

float stddev(float* a, int n){
  if(n<=1) return 0.0f;
  double s=0, s2=0;
  for(int i=0;i<n;i++){ s+=a[i]; s2+=a[i]*a[i]; }
  double v = (s2/n) - (s/n)*(s/n);
  return (v>0) ? (float)sqrt(v) : 0.0f;
}

void pushEnvSample(float mag, float r, float p){
  magBuf[bufIdx]=mag; rollBuf[bufIdx]=r; pitchBuf[bufIdx]=p;
  bufIdx=(bufIdx+1)%BUF_N;
  if(bufCount<BUF_N) bufCount++;
}

/* ══════════════════════ NRF Send ════════════════════════════════════ */
bool nrfSendCoords(float lat, float lng, const String& reason){
  if(!nrfReady) return false;
  char buf[NRF_PAYLOAD] = {0};
  String coord = String(DEVICE_TOKEN) + "," + String(lat,6) + "," + String(lng,6);
  coord.toCharArray(buf, NRF_PAYLOAD);
  bool ok = radio.write(buf, NRF_PAYLOAD);
  lastEvent = ok ? ("NRF sent: " + reason) : "NRF send failed";
  Serial.println(ok ? ("[NRF] Sent: " + coord) : "[NRF] Send FAILED");
  return ok;
}

/* Send a TEST packet (used by burst test and simulation drills) */
bool nrfSendTest(uint8_t seq, const String& tag) {
  if(!nrfReady) return false;
  char buf[NRF_PAYLOAD] = {0};
  snprintf(buf, NRF_PAYLOAD, "TEST,%02u,%s", seq, tag.substring(0,12).c_str());
  bool ok = radio.write(buf, NRF_PAYLOAD);
  return ok;
}

/* ══════════════════ Alert control ═══════════════════════════════════ */
void startSOSAlert(){
  if(alertActive) return;
  alertActive  = true;
  sosIdx       = 0;
  bool on      = SOS_STEPS[0].on;
  sosNextChange = millis() + SOS_STEPS[0].ms;
  digitalWrite(ALERT_LED_PIN, on ? HIGH : LOW);
  if(on) buzzerOn(3000); else buzzerOff();
}

void stopSOSAlert(){
  alertActive = false;
  digitalWrite(ALERT_LED_PIN, LOW);
  buzzerOff();
}

/* ══════════════════ Web handlers ════════════════════════════════════ */
String jsonStatus(){
  float aMag = sqrt(ax*ax + ay*ay + az*az);
  String s = "{";
  s += "\"armed\":"      + String(armed     ? "true":"false") + ",";
  s += "\"hasFix\":"     + String(hasFix    ? "true":"false") + ",";
  s += "\"lat\":"        + String(lastLat,6)                  + ",";
  s += "\"lng\":"        + String(lastLng,6)                  + ",";
  s += "\"roll\":"       + String(roll,2)                     + ",";
  s += "\"pitch\":"      + String(pitch,2)                    + ",";
  s += "\"aMag\":"       + String(aMag,3)                     + ",";
  s += "\"ay\":"         + String(ay,3)                       + ",";
  s += "\"gx\":"         + String(gx,1)                       + ",";
  s += "\"nrfReady\":"   + String(nrfReady  ? "true":"false") + ",";
  s += "\"fallState\":"  + String((int)fallState)             + ",";
  s += "\"simEvent\":"   + String((int)simEvent)              + ",";
  s += "\"inSkid\":"     + String(inSkid    ? "true":"false") + ",";
  s += "\"lastEvent\":\"" + lastEvent + "\"";
  s += "}";
  return s;
}

String jsonDiagStats(){
  uint32_t avgFixMs  = gpsFixCount     > 0 ? gpsTotalFixMs    / gpsFixCount     : 0;
  uint32_t avgRespMs = fallConfirmCount > 0 ? fallTotalRespMs  / fallConfirmCount : 0;
  uint32_t totalFall = fallCancelCount + fallConfirmCount;
  float    fpr       = totalFall > 0 ? (100.0f * fallConfirmCount / totalFall) : 0.0f;

  String s = "{";
  s += "\"gpsFixCount\":"    + String(gpsFixCount)    + ",";
  s += "\"gpsAvgFixMs\":"    + String(avgFixMs)        + ",";
  s += "\"fallEvents\":"     + String(fallEventCount)  + ",";
  s += "\"fallConfirmed\":"  + String(fallConfirmCount)+ ",";
  s += "\"fallCanceled\":"   + String(fallCancelCount) + ",";
  s += "\"fallAvgRespMs\":"  + String(avgRespMs)       + ",";
  s += "\"fallFPR\":"        + String(fpr,1)           + ",";
  s += "\"skidCount\":"      + String(skidCount)       + ",";
  s += "\"impactCount\":"    + String(impactCount)     + ",";
  s += "\"burstDone\":"      + String(burstDone)       + ",";
  s += "\"burstRunning\":"   + String(burstPending ? "true":"false");
  s += "}";
  return s;
}

String jsonBurstResults(){
  String s = "{\"done\":" + String(burstDone) + ",\"running\":" + String(burstPending?"true":"false");
  s += ",\"results\":[";
  for(int i=0; i<BURST_N; i++){
    s += "{\"ok\":" + String(burstResults[i].ok?"true":"false");
    s += ",\"ms\":"  + String(burstResults[i].delayMs) + "}";
    if(i < BURST_N-1) s += ",";
  }
  s += "]}";
  return s;
}

void handleRoot()        { server.send(200,"text/html",        WEB_INDEX_HTML);   }
void handleStatus()      { server.send(200,"application/json", jsonStatus());      }
void handleDiagStats()   { server.send(200,"application/json", jsonDiagStats());   }
void handleBurstResults(){ server.send(200,"application/json", jsonBurstResults());}

void handleArm(){
  armed=true;
  lastEvent="System armed";
  server.send(200,"text/plain","armed");
}
void handleDisarm(){
  armed=false; stopSOSAlert();
  lastEvent="System disarmed";
  server.send(200,"text/plain","disarmed");
}

/* Single legacy test (backward compatible) */
void handleTestNRF(){
  if(!nrfReady){ server.send(500,"text/plain","NRF not ready"); return; }
  float testLat = hasFix ? lastLat : 0.0f;
  float testLng = hasFix ? lastLng : 0.0f;
  bool ok = nrfSendCoords(testLat,testLng, hasFix?"Manual test":"Manual test (no fix)");
  server.send(ok?200:500,"text/plain",ok?"NRF test OK":"NRF test failed");
}

/* Start a 20-shot burst (non-blocking, runs in loop()) */
void handleRunBurst(){
  if(!nrfReady){ server.send(500,"text/plain","NRF not ready"); return; }
  if(burstPending){ server.send(409,"text/plain","Burst already running"); return; }
  memset(burstResults,0,sizeof(burstResults));
  burstCurrent = 0;
  burstDone    = 0;
  burstPending = true;
  burstNextT   = millis();
  lastEvent    = "Burst test started (20 packets)";
  server.send(200,"text/plain","ok");
}

/* Trigger a simulation event */
void handleSimulate(){
  String ev = server.arg("event");
  if      (ev=="skid")  { simEvent=SIM_SKID;   }
  else if (ev=="fall")  { simEvent=SIM_FALL;   }
  else if (ev=="impact"){ simEvent=SIM_IMPACT; }
  else { server.send(400,"text/plain","unknown event"); return; }

  simStart  = millis();
  simMode   = true;
  lastEvent = "SIM START: " + ev + " drill";
  Serial.println("[SIM] Starting simulation: " + ev);
  server.send(200,"text/plain","sim:"+ev);
}

/* Stop simulation */
void handleSimStop(){
  simEvent=SIM_NONE; simMode=false;
  ax=0; ay=0; az=1.0f; gx=gy=gz=0;
  fallState=FS_IDLE; inSkid=false;
  lastEvent="SIM stopped";
  server.send(200,"text/plain","stopped");
}

/* ══════════════════════════ setup() ═════════════════════════════════ */
void setup(){
  Serial.begin(115200);
  delay(200);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  Wire.begin();
  mpuWrite(0x6B, 0x00);
  delay(100);

  Serial.println("Calibrating MPU-6050 (hold still)…");
  calibrateMPU();
  Serial.println("Calibration done.");

  gpsSearchStart = millis();   // start GPS fix timer

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP: "); Serial.println(WiFi.softAPIP());

  if(!radio.begin()){
    Serial.println("[NRF] Init FAILED");
    nrfReady = false;
  } else {
    radio.setPayloadSize(NRF_PAYLOAD);
    radio.openWritingPipe(NRF_ADDRESS);
    radio.stopListening();
    radio.setPALevel(RF24_PA_HIGH);
    Serial.println("[NRF] Ready.");
    nrfReady = true;
  }

  server.on("/",               handleRoot);
  server.on("/status.json",    handleStatus);
  server.on("/diag-stats.json",handleDiagStats);
  server.on("/burst-results.json", handleBurstResults);
  server.on("/arm",            handleArm);
  server.on("/disarm",         handleDisarm);
  server.on("/test-nrf",       handleTestNRF);
  server.on("/run-burst",      handleRunBurst);
  server.on("/simulate",       handleSimulate);
  server.on("/sim-stop",       handleSimStop);
  server.begin();

  pinMode(ALERT_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN,    OUTPUT);
  digitalWrite(ALERT_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN,    LOW);

  Serial.println("GPS Danger Monitor v3.0 ready.");
}

/* ══════════════════════════ loop() ══════════════════════════════════ */
void loop(){
  static uint32_t lastTick = millis();
  uint32_t now = millis();
  float dt = (now - lastTick) / 1000.0f;
  if(dt <= 0) dt = 0.001f;
  lastTick = now;

  server.handleClient();

  /* ── Non-blocking NRF burst test ──────────────────────────────── */
  if(burstPending && burstCurrent < BURST_N && now >= burstNextT){
    char buf[NRF_PAYLOAD] = {0};
    snprintf(buf, NRF_PAYLOAD, "TEST,%02d,%lu", burstCurrent+1, now);
    uint32_t t0 = micros();
    bool ok = radio.write(buf, NRF_PAYLOAD);
    uint32_t dt_us = micros() - t0;
    burstResults[burstCurrent].ok      = ok;
    burstResults[burstCurrent].delayMs = (uint16_t)(dt_us / 1000);
    burstCurrent++;
    burstNextT = now + 100;  // 100 ms between packets
    burstDone  = burstCurrent;
    if(burstCurrent >= BURST_N){
      burstPending = false;
      lastEvent = "Burst test complete (" + String(BURST_N) + " packets)";
      Serial.println("[BURST] Done.");
    }
  }

  /* ── GPS feed ──────────────────────────────────────────────────── */
  while(gpsSerial.available()){
    char c = gpsSerial.read();
    if(gps.encode(c)){
      if(gps.location.isValid() && !hasFix){
        // Acquired fix
        uint32_t fixTime = now - gpsSearchStart;
        gpsTotalFixMs += fixTime;
        gpsFixCount++;
        hasFix  = true;
        lastLat = gps.location.lat();
        lastLng = gps.location.lng();
        Serial.printf("[GPS] Fix acquired in %lu ms. Count: %d\n", fixTime, gpsFixCount);
      } else if(gps.location.isValid() && hasFix){
        lastLat = gps.location.lat();
        lastLng = gps.location.lng();
      }
    }
  }
  // Detect stale fix (age > 8 s → mark as lost)
  if(hasFix && gps.location.age() > 8000){
    hasFix = false;
    gpsSearchStart = now;
    Serial.println("[GPS] Fix lost – searching…");
  }

  /* ── IMU read + simulation override ──────────────────────────── */
  readMPU();
  if(simEvent != SIM_NONE) applySimulation();  // override ax/ay/az/gx/gy/gz
  updateOrientation(dt);

  float aMag = sqrt(ax*ax + ay*ay + az*az);
  pushEnvSample(aMag, roll, pitch);

  /* ══════════════ Fall-Impact-Stillness FSM ══════════════════════ */
  if(armed){
    switch(fallState){

      case FS_IDLE:
        if(aMag < FREEFALL_G){
          fallState = FS_FREEFALL;
          fsTimer   = now;
        }
        break;

      case FS_FREEFALL:
        if(aMag >= FREEFALL_G){
          fallState = FS_IDLE;   // brief dip – not real freefall
        } else if(now - fsTimer >= FREEFALL_DUR_MS){
          fallState    = FS_WAIT_IMPACT;
          fsTimer      = now;
          fallStartMs  = now;    // start responsiveness timer
          fallEventCount++;
          lastEvent    = "Freefall confirmed – awaiting impact…";
          Serial.println("[FALL] Freefall confirmed.");
        }
        break;

      case FS_WAIT_IMPACT:
        if(aMag > IMPACT_G){
          fallState  = FS_STILLNESS;
          fsTimer    = now;
          stillIdx   = stillCount = 0;
          lastEvent  = "Impact detected – checking stillness…";
          Serial.println("[FALL] Impact spike!");
        } else if(now - fsTimer > IMPACT_WINDOW_MS){
          fallState = FS_IDLE;
          fallCancelCount++;
          lastEvent = "No impact in window – cancelled"; //Sudden Accel but no Impact. For Data Recording
          Serial.println("[FALL] No impact – cancel.");
        }
        break;

      case FS_STILLNESS:
        stillBuf[stillIdx % STILL_BUF_N] = aMag;
        stillIdx++;
        stillCount = min(stillIdx, STILL_BUF_N);

        if(now - fsTimer >= STILLNESS_DUR_MS){
          float sig = stddev(stillBuf, stillCount);
          if(sig < STILLNESS_STD_MAX){
            fallState = FS_CONFIRMED;
          } else {
            fallState = FS_IDLE;
            fallCancelCount++;
            lastEvent = "Post-impact movement – user OK (σ=" + String(sig,3) + ")";
            Serial.println("[FALL] User conscious – cancel.");
          }
        }
        break;

      case FS_CONFIRMED:
        fallConfirmCount++;
        fallTotalRespMs += (now - fallStartMs);
        lastEvent = "FALL CONFIRMED – alert sent!";
        Serial.printf("[ALERT] Fall confirmed. Response: %lu ms\n", now - fallStartMs);

        if(!simMode){
          nrfSendCoords(lastLat, lastLng, "Fall confirmed");
        } else {
          nrfSendTest(99, "SIMFALL");
          lastEvent = "SIM FALL confirmed (drill – no real alert)";
        }
        startSOSAlert();
        fallState = FS_IDLE;
        break;
    }
  } else {
    fallState = FS_IDLE;
  }

  /* ══════════ Skid / Slide Detector (parallel, armed + FSM idle) ═══
   *  Ref: Motorcycle crash paper (0.70g lateral, 96% accuracy)
   *       Airbag paper (angular velocity ≥ 30°/s)
   */
  if(armed && fallState == FS_IDLE){
    bool skidCond = (fabsf(ay) > SKID_LATERAL_G) && (fabsf(gx) > SKID_ROLL_RATE);

    if(skidCond){
      if(!inSkid){ inSkid = true; skidTimer = now; }
      else if(now - skidTimer >= SKID_DUR_MS){
        skidCount++;
        lastEvent = "SKID DETECTED – alert!";
        Serial.printf("[ALERT] Skid: ay=%.2fg gx=%.0f°/s\n", ay, gx);

        if(!simMode){
          nrfSendCoords(lastLat, lastLng, "Skid detected");
        } else {
          nrfSendTest(98, "SIMSKID");
          lastEvent = "SIM SKID confirmed (drill)";
        }
        startSOSAlert();
        inSkid = false;
      }
    } else {
      inSkid = false;
    }
  }

  /* ══════════ Direct-Impact Detector (no freefall precondition) ════
   *  Ref: Sensors 2022 – LF falls produce 4–9g peaks
   */
  if(armed && fallState == FS_IDLE && !inSkid){
    if(aMag > IMPACT_DIRECT_G && (now - lastImpactAlert) > IMPACT_DEBOUNCE){
      lastImpactAlert = now;
      impactCount++;
      lastEvent = "DIRECT IMPACT (" + String(aMag,1) + "g) – alert!";
      Serial.printf("[ALERT] Direct impact: aMag=%.1fg\n", aMag);

      if(!simMode){
        nrfSendCoords(lastLat, lastLng, "Direct impact");
      } else {
        nrfSendTest(97, "SIMIMPACT");
        lastEvent = "SIM IMPACT confirmed (drill)";
      }
      startSOSAlert();
    }
  }

  /* ══════════ Environmental / Structural Instability ══════════════ */
  if(now - lastAnalyze >= ANALYZE_EVERY_MS && bufCount >= 10){
    lastAnalyze = now;

    float tmpMag[BUF_N], tmpRoll[BUF_N], tmpPitch[BUF_N];
    int n = bufCount;
    for(int i=0;i<n;i++){
      int idx = (bufIdx - n + i + BUF_N) % BUF_N;
      tmpMag[i]   = magBuf[idx];
      tmpRoll[i]  = rollBuf[idx];
      tmpPitch[i] = pitchBuf[idx];
    }
    float sMag   = stddev(tmpMag,   n);
    float sRoll  = stddev(tmpRoll,  n);
    float sPitch = stddev(tmpPitch, n);

    bool groundShock = (sMag >= ACC_STD_DANGER) && (sRoll < ROLL_PITCH_STD && sPitch < ROLL_PITCH_STD);
    bool waveMotion  = (sMag >= ACC_STD_DANGER*0.6f) && (sRoll >= ROLL_PITCH_STD || sPitch >= ROLL_PITCH_STD);

    if(armed && !simMode){
      if(groundShock){
        lastEvent = "Structural impact / high drop";
        nrfSendCoords(lastLat, lastLng, "Ground shock");
        startSOSAlert();
      } else if(waveMotion){
        lastEvent = "Unstable wave motion";
        nrfSendCoords(lastLat, lastLng, "Wave motion");
        startSOSAlert();
      }
    }

    if(streamToSerial){
      Serial.printf("A_m=%.3f ay=%.2f  roll=%.1f  pitch=%.1f  fix=%d  armed=%d  fall=%d  sim=%d | %s\n",
                    aMag, ay, roll, pitch, (int)hasFix,
                    (int)armed, (int)fallState, (int)simEvent, lastEvent.c_str());
    }
  }

  /* ══════════ Non-blocking SOS pattern driver ═══════════════════ */
  if(alertActive && armed){
    if(millis() >= sosNextChange){
      sosIdx = (sosIdx+1) % SOS_N;
      bool on = SOS_STEPS[sosIdx].on;
      digitalWrite(ALERT_LED_PIN, on ? HIGH : LOW);
      if(on) buzzerOn(3000); else buzzerOff();
      sosNextChange = millis() + SOS_STEPS[sosIdx].ms;
    }
  } else if(!armed && alertActive){
    stopSOSAlert();
  }
}
