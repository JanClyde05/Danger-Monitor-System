/*  GPS_Danger_Monitor.ino  –  v2.0  (Fall-Impact-Stillness Edition)
 *  ESP32 + TinyGPS++ + MPU-6050 + NRF24L01 + WebServer (AP mode)
 *
 *  Detection pipeline (thesis-grade):
 *    1. FREEFALL   – A_m < 0.4 g  for ≥ 100 ms
 *    2. IMPACT     – A_m > 3.0 g  within 500 ms of freefall end
 *    3. STILLNESS  – σ(A_m)< 0.15 g over 2 s after impact (unconscious check)
 *  All three must fire in sequence before an alert is raised.
 *
 *  Libraries: TinyGPSPlus (Mikal Hart), RF24 (TMRh20)
 *
 *  Pins (edit if needed):
 *    GPS  UART1  : RX=16, TX=17 @ 9600
 *    MPU6050 I2C : SDA=21, SCL=22, addr=0x68
 *    NRF24       : MOSI=23, MISO=19, SCK=18, CE=2, CSN=15
 *    LED         : GPIO 4
 *    Buzzer      : GPIO 5
 */

#include <WiFi.h>
#include <WebServer.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <SPI.h>
#include <RF24.h>
#include "web_ui.h"

/* ═══════════════════════ USER CONFIG ════════════════════════ */
static const int    GPS_RX   = 16;
static const int    GPS_TX   = 17;
static const uint32_t GPS_BAUD = 9600;

const char*  AP_SSID    = "GPS Danger Monitor";
const char*  AP_PASS    = "";
IPAddress    apIP       (192,168,4,1);
IPAddress    apGateway  (192,168,4,1);
IPAddress    apSubnet   (255,255,255,0);

/* ── Fall-Impact-Stillness thresholds ── */
const float    FREEFALL_G          = 0.40f;  // g  – below this = freefall
const uint32_t FREEFALL_DUR_MS     = 100;    // ms – freefall must persist
const float    IMPACT_G            = 3.00f;  // g  – spike above = impact
const uint32_t IMPACT_WINDOW_MS    = 500;    // ms – window after freefall
const uint32_t STILLNESS_DUR_MS    = 2000;   // ms – observe for stillness
const float    STILLNESS_STD_MAX   = 0.15f;  // g  – σ below = person still

/* ── Environmental instability (secondary alert) ── */
const float    ACC_STD_DANGER   = 0.40f;
const float    ROLL_PITCH_STD   = 10.0f;
const uint32_t ANALYZE_EVERY_MS = 1000;

const uint32_t CALIB_MS         = 3000;
/* ════════════════════════════════════════════════════════════ */

/* ── Hardware ── */
TinyGPSPlus    gps;
HardwareSerial gpsSerial(1);
WebServer      server(80);

#define NRF_CE  2
#define NRF_CSN 15
RF24 radio(NRF_CE, NRF_CSN);
const uint64_t NRF_ADDRESS = 0xE8E8F0F01LL;
const uint8_t  NRF_PAYLOAD = 32;          // fixed payload – must match receiver

#define ALERT_LED_PIN 4
#define BUZZER_PIN    5

/* ── MPU-6050 ── */
const uint8_t MPU_ADDR = 0x68;
float ax, ay, az, gx, gy, gz;
float aBiasX=0, aBiasY=0, aBiasZ=0;
float gBiasX=0, gBiasY=0, gBiasZ=0;
float roll=0, pitch=0;
const float ALPHA = 0.02f;

/* ── GPS state (Last-Known-Location strategy) ── */
float lastLat=0, lastLng=0;
bool  hasFix=false;

/* ── System state ── */
bool    armed     = true;
String  lastEvent = "System boot";
uint32_t lastAnalyze = 0;
bool    nrfReady  = false;
bool    streamToSerial = true;

/* ── Rolling statistics buffers (environmental analysis) ── */
const int BUF_N = 50;
float magBuf[BUF_N], rollBuf[BUF_N], pitchBuf[BUF_N];
int   bufIdx=0, bufCount=0;

/* ── Stillness buffer (post-impact) ── */
const int STILL_BUF_N = 40;
float stillBuf[STILL_BUF_N];
int   stillIdx=0, stillCount=0;

/* ══════════════ Fall State Machine ══════════════ */
enum FallState { FS_IDLE, FS_FREEFALL, FS_WAIT_IMPACT, FS_STILLNESS, FS_CONFIRMED };
FallState fallState    = FS_IDLE;
uint32_t  fsTimer      = 0;      // generic per-state timestamp

/* ══════════════ SOS Alert (non-blocking) ════════ */
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

/* ═══════════════════════ Utilities ═════════════════════════ */

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
  Wire.read(); Wire.read();                    // skip temperature
  int16_t rGx=(Wire.read()<<8)|Wire.read();
  int16_t rGy=(Wire.read()<<8)|Wire.read();
  int16_t rGz=(Wire.read()<<8)|Wire.read();

  ax = (rAx/16384.0f) - aBiasX;
  ay = (rAy/16384.0f) - aBiasY;
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
    sgx+=gx; sgy+=gy; sgz+=gz;
    delay(CALIB_MS/N);
  }
  aBiasX=sax/N; aBiasY=say/N; aBiasZ=saz/N-1.0f;
  gBiasX=sgx/N; gBiasY=sgy/N; gBiasZ=sgz/N;
}

void updateOrientation(float dt){
  float aRoll  = atan2(ay,az)*57.29578f;
  float aPitch = atan2(-ax,sqrt(ay*ay+az*az))*57.29578f;
  roll  += gx*dt;  pitch += gy*dt;
  roll  = (1.0f-ALPHA)*roll  + ALPHA*aRoll;
  pitch = (1.0f-ALPHA)*pitch + ALPHA*aPitch;
}

/* σ =  √( (Σx²/n) − (Σx/n)² ) */
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

/* ══════════════════════ NRF Send ════════════════════════════ */
/*
 * Payload format:  "LAT,LNG"  (e.g. "14.599512,120.984222")
 * The receiver will construct the full Netlify URL from this.
 */
bool nrfSendCoords(float lat, float lng, const String& reason){
  if(!nrfReady) return false;

  char buf[NRF_PAYLOAD] = {0};
  String coord = String(lat,6) + "," + String(lng,6);
  coord.toCharArray(buf, NRF_PAYLOAD);

  bool ok = radio.write(buf, NRF_PAYLOAD);
  if(ok){
    lastEvent = "NRF sent: " + reason;
    Serial.println("[NRF] Sent: " + coord + "  (" + reason + ")");
  } else {
    lastEvent = "NRF send failed";
    Serial.println("[NRF] Send FAILED");
  }
  return ok;
}

/* ══════════════════ Alert control ══════════════════════════ */
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

/* ══════════════════ Web handlers ════════════════════════════ */
String jsonStatus(){
  String s = "{";
  s += "\"armed\":"     + String(armed    ? "true":"false") + ",";
  s += "\"hasFix\":"    + String(hasFix   ? "true":"false") + ",";
  s += "\"lat\":"       + String(lastLat,6)                 + ",";
  s += "\"lng\":"       + String(lastLng,6)                 + ",";
  s += "\"roll\":"      + String(roll,2)                    + ",";
  s += "\"pitch\":"     + String(pitch,2)                   + ",";
  s += "\"aMag\":"      + String(sqrt(ax*ax+ay*ay+az*az),3) + ",";
  s += "\"nrfReady\":"  + String(nrfReady ? "true":"false") + ",";
  s += "\"fallState\":" + String((int)fallState)            + ",";
  s += "\"lastEvent\":\"" + lastEvent + "\"";
  s += "}";
  return s;
}

void handleRoot()   { server.send(200, "text/html",        WEB_INDEX_HTML); }
void handleStatus() { server.send(200, "application/json", jsonStatus());   }
void handleArm()    { armed=true;  lastEvent="System armed";
                      server.send(200,"text/plain","armed");   }
void handleDisarm() { armed=false; stopSOSAlert(); lastEvent="System disarmed";
                      server.send(200,"text/plain","disarmed"); }
void handleTestNRF(){
  if(!nrfReady){ server.send(500,"text/plain","NRF not ready"); return; }
  if(!hasFix)  { lastEvent="Test – no GPS fix";
                 server.send(409,"text/plain","No GPS fix yet"); return; }
  bool ok = nrfSendCoords(lastLat,lastLng,"Manual test");
  server.send(ok?200:500,"text/plain", ok?"NRF test OK":"NRF test failed");
}

/* ══════════════════════════ setup() ═════════════════════════ */
void setup(){
  Serial.begin(115200);
  delay(200);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  Wire.begin();
  mpuWrite(0x6B, 0x00);   // wake MPU-6050
  delay(100);

  Serial.println("Calibrating MPU-6050 (hold still)…");
  calibrateMPU();
  Serial.println("Calibration done.");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP: "); Serial.println(WiFi.softAPIP());

  if(!radio.begin()){
    Serial.println("[NRF] Init FAILED");
    nrfReady = false;
  } else {
    radio.setPayloadSize(NRF_PAYLOAD);   // ← fixed payload on both ends
    radio.openWritingPipe(NRF_ADDRESS);
    radio.stopListening();
    radio.setPALevel(RF24_PA_HIGH);
    Serial.println("[NRF] Ready.");
    nrfReady = true;
  }

  server.on("/",          handleRoot);
  server.on("/status.json",handleStatus);
  server.on("/arm",       handleArm);
  server.on("/disarm",    handleDisarm);
  server.on("/test-nrf",  handleTestNRF);
  server.begin();

  pinMode(ALERT_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN,    OUTPUT);
  digitalWrite(ALERT_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN,    LOW);

  Serial.println("GPS Danger Monitor v2 ready.");
}

/* ══════════════════════════ loop() ══════════════════════════ */
void loop(){
  static uint32_t lastTick = millis();
  uint32_t now = millis();
  float dt = (now - lastTick) / 1000.0f;
  if(dt <= 0) dt = 0.001f;
  lastTick = now;

  server.handleClient();

  /* ── GPS feed ── */
  while(gpsSerial.available()){
    char c = gpsSerial.read();
    if(gps.encode(c) && gps.location.isValid()){
      /* Always update Last-Known-Location so dead zones still have a coord */
      lastLat = gps.location.lat();
      lastLng = gps.location.lng();
      hasFix  = true;
    }
  }

  /* ── IMU ── */
  readMPU();
  updateOrientation(dt);

  /* A_m = √(ax² + ay² + az²)  – orientation-independent resultant */
  float aMag = sqrt(ax*ax + ay*ay + az*az);
  pushEnvSample(aMag, roll, pitch);

  /* ══════════════ Fall-Impact-Stillness State Machine ══════════════ */
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
          /* Brief dip – not a real freefall, reset */
          fallState = FS_IDLE;
        } else if(now - fsTimer >= FREEFALL_DUR_MS){
          fallState = FS_WAIT_IMPACT;
          fsTimer   = now;
          lastEvent = "Freefall – awaiting impact…";
          Serial.println("[FALL] Freefall confirmed, watching for impact.");
        }
        break;

      case FS_WAIT_IMPACT:
        if(aMag > IMPACT_G){
          /* High-G spike within window → impact detected */
          fallState  = FS_STILLNESS;
          fsTimer    = now;
          stillIdx   = 0;
          stillCount = 0;
          lastEvent  = "Impact – checking stillness…";
          Serial.println("[FALL] Impact spike detected! Checking for stillness.");
        } else if(now - fsTimer > IMPACT_WINDOW_MS){
          /* No impact in time – probably a gentle put-down */
          fallState = FS_IDLE;
          Serial.println("[FALL] No impact – cancelling.");
        }
        break;

      case FS_STILLNESS:
        /* Collect samples for STILLNESS_DUR_MS, then evaluate σ */
        stillBuf[stillIdx % STILL_BUF_N] = aMag;
        stillIdx++;
        stillCount = min(stillIdx, STILL_BUF_N);

        if(now - fsTimer >= STILLNESS_DUR_MS){
          float sig = stddev(stillBuf, stillCount);
          if(sig < STILLNESS_STD_MAX){
            fallState = FS_CONFIRMED;   // person is motionless → alert
          } else {
            /* Person is moving post-impact → conscious, cancel alert */
            fallState = FS_IDLE;
            lastEvent = "Post-impact movement – user OK";
            Serial.println("[FALL] Movement post-impact – user appears conscious.");
          }
        }
        break;

      case FS_CONFIRMED:
        lastEvent = "FALL CONFIRMED – sending alert!";
        Serial.println("[ALERT] Fall confirmed: Freefall + Impact + Stillness.");
        nrfSendCoords(lastLat, lastLng, "Fall confirmed");
        startSOSAlert();
        fallState = FS_IDLE;
        break;
    }
  } else {
    /* System disarmed – keep state machine idle */
    fallState = FS_IDLE;
  }

  /* ══════════════ Environmental / Structural Instability ══════════════ */
  if(now - lastAnalyze >= ANALYZE_EVERY_MS && bufCount >= 10){
    lastAnalyze = now;

    /* Copy ring-buffer into linear arrays for stddev */
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

    bool groundShock = (sMag >= ACC_STD_DANGER)
                    && (sRoll < ROLL_PITCH_STD && sPitch < ROLL_PITCH_STD);
    bool waveMotion  = (sMag >= ACC_STD_DANGER*0.6f)
                    && (sRoll >= ROLL_PITCH_STD || sPitch >= ROLL_PITCH_STD);

    if(armed){
      if(groundShock){
        lastEvent = "Structural impact / high drop detected";
        Serial.println("[WARN] Ground shock pattern.");
        nrfSendCoords(lastLat, lastLng, "Ground shock");
        startSOSAlert();
      } else if(waveMotion){
        lastEvent = "Unstable wave motion detected";
        Serial.println("[WARN] Wave / tumble pattern.");
        nrfSendCoords(lastLat, lastLng, "Wave motion");
        startSOSAlert();
      }
    }

    if(streamToSerial){
      Serial.printf("A_m=%.3f  roll=%.1f  pitch=%.1f  fix=%d  lat=%.6f  lng=%.6f"
                    "  armed=%d  fall=%d  | %s\n",
                    aMag, roll, pitch, (int)hasFix, lastLat, lastLng,
                    (int)armed, (int)fallState, lastEvent.c_str());
    }
  }

  /* ══════════════ Non-blocking SOS pattern driver ══════════════ */
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
