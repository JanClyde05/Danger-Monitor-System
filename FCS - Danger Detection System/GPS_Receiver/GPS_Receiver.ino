/*  GPS_Receiver.ino  –  v2.2
 *  Pro Micro (ATmega32U4) + NRF24L01  →  HID Keyboard
 *
 *  Receives two payload types from the ESP32 transmitter:
 *
 *  1. EMERGENCY payload  "TOKEN,LAT,LNG"
 *     → Opens:  https://danger-monitor-profile.netlify.app/?token=DM-001&loc=LAT,LNG
 *
 *  2. TEST payload       "TEST,NN,<tag>"  (burst test and sim drills)
 *     → Opens:  https://danger-monitor-profile.netlify.app/?nrftest=1&seq=NN&ts=<millis>
 *     The Netlify page shows a "Test Received" notification instead of the emergency profile.
 *     Transmission delay is measured on the transmitter side via RF24 ACK timing;
 *     the seq number allows correlation of results in the dashboard.
 *
 *  Wiring:
 *    NRF24  CE  → pin 9
 *    NRF24  CSN → pin 10
 *    NRF24  MOSI→ MOSI (pin 16 on Pro Micro)
 *    NRF24  MISO→ MISO (pin 14)
 *    NRF24  SCK → SCK  (pin 15)
 *    NRF24  VCC → 3.3 V  (do NOT use 5 V!)
 *    LED_BUILTIN → blinks on every received packet
 */

#include <SPI.h>
#include <RF24.h>
#include <Keyboard.h>

#define CE_PIN   9
#define CSN_PIN 10

RF24 radio(CE_PIN, CSN_PIN);

const uint64_t NRF_ADDRESS  = 0xE8E8F0F01LL;
const uint8_t  NRF_PAYLOAD  = 32;              // must match transmitter

/* ── Netlify base URLs ─────────────────────────────────────────────── */
const char* NETLIFY_BASE      = "https://danger-monitor-profile.netlify.app/?token=";
const char* NETLIFY_TEST_BASE = "https://danger-monitor-profile.netlify.app/?nrftest=1&seq=";

#define LED_PIN  LED_BUILTIN

/* ── Debounce timer ─────────────────────────────────────────────────  */
uint32_t lastTrigger = 0;
const uint32_t DEBOUNCE_MS = 1500;   // ignore re-transmissions within this window

/* ─────────────────────────────────────────────────────────────────── */
void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if(!radio.begin()){
    Serial.println("[NRF] Init FAILED – check wiring!");
    while(true){
      digitalWrite(LED_PIN, HIGH); delay(100);
      digitalWrite(LED_PIN, LOW);  delay(100);
    }
  }

  radio.setPayloadSize(NRF_PAYLOAD);
  radio.openReadingPipe(0, NRF_ADDRESS);
  radio.setPALevel(RF24_PA_HIGH);
  radio.startListening();

  Keyboard.begin();
  Serial.println("[NRF] Receiver v2.2 ready.");
}

/* ─────────────────────────────────────────────────────────────────── */
void loop(){
  if(!radio.available()) return;

  char payload[NRF_PAYLOAD + 1] = {0};
  radio.read(payload, NRF_PAYLOAD);
  String raw = String(payload);
  raw.trim();

  uint32_t now = millis();

  /* ── LED blink on every reception ── */
  digitalWrite(LED_PIN, HIGH); delay(60); digitalWrite(LED_PIN, LOW);

  /* ── Detect payload type ─────────────────────────────────────── */
  if(raw.startsWith("TEST,")){
    /* TEST payload from burst test or simulation drill.
     * Format: "TEST,NN,tag"
     * NN  = 2-digit sequence number (01–20 for burst, 97-99 for sim drills)
     */
    Serial.print("[NRF] TEST payload: "); Serial.println(raw);

    // Extract sequence number (chars 5-6)
    String seq = "";
    int firstComma  = raw.indexOf(',');
    int secondComma = raw.indexOf(',', firstComma + 1);
    if(firstComma >= 0 && secondComma > firstComma){
      seq = raw.substring(firstComma + 1, secondComma);
    } else if(firstComma >= 0){
      seq = raw.substring(firstComma + 1);
    }

    if((now - lastTrigger) < DEBOUNCE_MS){
      Serial.println("[NRF] TEST debounced – skipped");
      return;
    }
    lastTrigger = now;

    /* Type the test notification URL */
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press('l');
    Keyboard.releaseAll();
    delay(220);

    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press('a');
    Keyboard.releaseAll();
    delay(70);

    Keyboard.print(NETLIFY_TEST_BASE);   // "...?nrftest=1&seq="
    Keyboard.print(seq);                  // e.g. "05"
    Keyboard.print("&ts=");
    Keyboard.print(String(now));          // receiver-side millis (informational)
    delay(70);
    Keyboard.write(KEY_RETURN);

    Serial.print("[HID] Test notification: seq="); Serial.println(seq);

  } else {
    /* EMERGENCY payload: "TOKEN,LAT,LNG" */
    int firstComma  = raw.indexOf(',');
    int secondComma = raw.indexOf(',', firstComma + 1);

    if(firstComma < 0 || secondComma < 0){
      Serial.print("[NRF] Malformed payload ignored: "); Serial.println(raw);
      return;
    }

    String token  = raw.substring(0, firstComma);
    String coords = raw.substring(firstComma + 1);

    Serial.print("[NRF] EMERGENCY | Token: "); Serial.print(token);
    Serial.print("  Coords: ");               Serial.println(coords);

    if((now - lastTrigger) < DEBOUNCE_MS){
      Serial.println("[NRF] EMERGENCY debounced – skipped");
      return;
    }
    lastTrigger = now;

    /* HID sequence → open emergency profile */
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press('l');
    Keyboard.releaseAll();
    delay(250);

    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press('a');
    Keyboard.releaseAll();
    delay(80);

    Keyboard.print(NETLIFY_BASE);    // ".../?token="
    Keyboard.print(token);           // e.g. "DM-001"
    Keyboard.print("&loc=");
    Keyboard.print(coords);          // e.g. "14.599512,120.984222"
    delay(80);
    Keyboard.write(KEY_RETURN);

    Serial.print("[HID] Emergency URL → token="); Serial.print(token);
    Serial.print("  loc="); Serial.println(coords);
  }
}
