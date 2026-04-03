/*  GPS_Receiver.ino  –  v2.1
 *  Pro Micro (ATmega32U4) + NRF24L01  →  HID Keyboard
 *
 *  Receives "TOKEN,LAT,LNG" from the ESP32 transmitter and opens the
 *  Netlify alert page by typing the URL into the active browser tab.
 *
 *  URL pattern sent:
 *    https://danger-monitor-profile.netlify.app/?token=DM-001&loc=LAT,LNG
 *
 *  Wiring:
 *    NRF24  CE  → pin 9
 *    NRF24  CSN → pin 10
 *    NRF24  MOSI→ MOSI (pin 16 on Pro Micro)
 *    NRF24  MISO→ MISO (pin 14)
 *    NRF24  SCK → SCK  (pin 15)
 *    NRF24  VCC → 3.3 V  (do NOT use 5 V)
 */

#include <SPI.h>
#include <RF24.h>
#include <Keyboard.h>

#define CE_PIN   9
#define CSN_PIN 10

RF24 radio(CE_PIN, CSN_PIN);

const uint64_t NRF_ADDRESS  = 0xE8E8F0F01LL;
const uint8_t  NRF_PAYLOAD  = 32;           // must match transmitter

/* ── Netlify base URL ─────────────────────────────────────── */
const char* NETLIFY_BASE = "https://danger-monitor-profile.netlify.app/?token=";

/* ── Optional: blink the built-in LED on receive ── */
#define LED_PIN  LED_BUILTIN

/* ─────────────────────────────────────────────────────────── */

void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if(!radio.begin()){
    Serial.println("[NRF] Init FAILED – check wiring!");
    // Blink fast to signal hardware fault
    while(true){
      digitalWrite(LED_PIN, HIGH); delay(100);
      digitalWrite(LED_PIN, LOW);  delay(100);
    }
  }

  radio.setPayloadSize(NRF_PAYLOAD);       // ← fixed payload, matches ESP32
  radio.openReadingPipe(0, NRF_ADDRESS);   // pipe 1 – pipe 0 is reserved for ACK handling
  radio.setPALevel(RF24_PA_HIGH);
  radio.startListening();

  Keyboard.begin();
  Serial.println("[NRF] Receiver ready. Waiting for GPS alert…");
}

void loop(){
  if(!radio.available()) return;

  /* ── Read payload ── */
  char payload[NRF_PAYLOAD + 1] = {0};
  radio.read(payload, NRF_PAYLOAD);

  String raw = String(payload);
  raw.trim();

  /* Sanity check – payload must be "TOKEN,LAT,LNG" → needs at least 2 commas */
  int firstComma = raw.indexOf(',');
  int secondComma = raw.indexOf(',', firstComma + 1);
  if(firstComma < 0 || secondComma < 0){
    Serial.print("[NRF] Ignored malformed payload: ");
    Serial.println(raw);
    return;
  }

  /* Split: token = everything before first comma, loc = everything after */
  String token  = raw.substring(0, firstComma);          // e.g. "DM-001"
  String coords = raw.substring(firstComma + 1);          // e.g. "14.599512,120.984222"

  Serial.print("[NRF] Token: ");   Serial.print(token);
  Serial.print("  Coords: ");      Serial.println(coords);

  /* Blink LED to confirm reception */
  digitalWrite(LED_PIN, HIGH); delay(80); digitalWrite(LED_PIN, LOW);

  /* ── HID sequence ── */

  /* 1. Focus the browser address bar with Ctrl+L */
  Keyboard.press(KEY_LEFT_CTRL);
  Keyboard.press('l');
  Keyboard.releaseAll();
  delay(250);                              // wait for address bar to open

  /* 2. Clear any existing text */
  Keyboard.press(KEY_LEFT_CTRL);
  Keyboard.press('a');
  Keyboard.releaseAll();
  delay(80);

  /* 3. Type the full Netlify alert URL with token and coordinates */
  Keyboard.print(NETLIFY_BASE);           // "...netlify.app/?token="
  Keyboard.print(token);                  // e.g. "DM-001"
  Keyboard.print("&loc=");
  Keyboard.print(coords);                 // e.g. "14.599512,120.984222"
  delay(80);

  /* 4. Navigate */
  Keyboard.write(KEY_RETURN);

  Serial.print("[HID] Opened: ");
  Serial.print(NETLIFY_BASE);
  Serial.print(token);
  Serial.print("&loc=");
  Serial.println(coords);

  delay(1500);                             // debounce – ignore rapid retransmissions
}
