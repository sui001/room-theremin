/*
 * PROJECT: Room theremin
 * DEVICE:  LD2450 radar listener, single node, USB serial to laptop
 *
 * Reads up to three bodies from an HLK-LD2450 mmWave radar and passes their
 * positions to the laptop as one JSON line per radar frame. The laptop
 * (room_theremin.html) does all the sound and mapping, so this node stays
 * dumb and robust: it only listens to the room and reports who is in it.
 *
 * CONFIGURATION:
 *   RADAR_RX_PIN / RADAR_TX_PIN: UART pins to the sensor
 *   STALE_MS: how long without a frame before we say the radar has gone quiet
 *
 * WIRING (ESP32-S3 SuperMini):
 *   LD2450 TX  -> GPIO4  (RADAR_RX_PIN)
 *   LD2450 RX  -> GPIO5  (RADAR_TX_PIN, only needed for config commands)
 *   LD2450 5V  -> 5V
 *   LD2450 GND -> GND
 *   Onboard RGB LED is GPIO47 on this board.
 *
 * SERIAL OUT (USB CDC, one line per frame, ~10 Hz):
 *   {"t":[[x,y,v],[x,y,v],null]}   x,y in mm (sensor at origin, y = away from it),
 *                                   v = speed in cm/s, null = empty slot
 *   {"stale":1}                     no radar frame for STALE_MS
 *   lines starting with # are human-readable status
 */

#include <Adafruit_NeoPixel.h>

#define VERSION "1.0"

// ============================================
// CONFIGURATION
// ============================================
const int RADAR_RX_PIN = 4;
const int RADAR_TX_PIN = 5;
const uint32_t RADAR_BAUD = 256000;   // LD2450 factory default

const int LED_PIN = 47;               // SuperMini onboard WS2812
const uint8_t LED_BRIGHTNESS = 40;

const uint32_t STALE_MS = 1000;
const uint32_t STALE_REPEAT_MS = 1000;

// LD2450 report frame: AA FF 03 00 | 3 x 8 byte targets | 55 CC
const uint8_t FRAME_HEAD[4] = {0xAA, 0xFF, 0x03, 0x00};
const uint8_t FRAME_TAIL[2] = {0x55, 0xCC};
const int FRAME_LEN = 30;
const int MAX_TARGETS = 3;

// ============================================
// STATE
// ============================================
enum RoomState { RADAR_SILENT, ROOM_EMPTY, ROOM_OCCUPIED };

struct Body {
  bool present;
  int16_t x;   // mm, left/right of sensor
  int16_t y;   // mm, distance out from sensor
  int16_t v;   // cm/s
};

Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial radar(1);

uint8_t frame[FRAME_LEN];
int framePos = 0;
Body bodies[MAX_TARGETS];
uint32_t lastFrameMs = 0;
uint32_t lastStaleMs = 0;
RoomState roomState = RADAR_SILENT;

// LD2450 signed values: top bit set = positive, clear = negative
int16_t decodeSigned(uint16_t raw) {
  return (raw & 0x8000) ? (int16_t)(raw & 0x7FFF) : -(int16_t)(raw & 0x7FFF);
}

uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

void readBodiesFromFrame() {
  for (int i = 0; i < MAX_TARGETS; i++) {
    const uint8_t *p = frame + 4 + i * 8;
    bool empty = true;
    for (int b = 0; b < 8; b++) if (p[b]) { empty = false; break; }
    bodies[i].present = !empty;
    if (empty) continue;
    bodies[i].x = decodeSigned(le16(p));
    bodies[i].y = decodeSigned(le16(p + 2));
    bodies[i].v = decodeSigned(le16(p + 4));
  }
}

void reportRoom() {
  char line[128];
  int n = snprintf(line, sizeof(line), "{\"t\":[");
  for (int i = 0; i < MAX_TARGETS; i++) {
    if (bodies[i].present)
      n += snprintf(line + n, sizeof(line) - n, "[%d,%d,%d]", bodies[i].x, bodies[i].y, bodies[i].v);
    else
      n += snprintf(line + n, sizeof(line) - n, "null");
    if (i < MAX_TARGETS - 1) n += snprintf(line + n, sizeof(line) - n, ",");
  }
  snprintf(line + n, sizeof(line) - n, "]}");
  Serial.println(line);
}

// Byte-wise sync: resyncs on the header after any garbage or dropped bytes
void listenToRadar() {
  while (radar.available()) {
    uint8_t c = radar.read();
    if (framePos < 4) {
      if (c == FRAME_HEAD[framePos]) frame[framePos++] = c;
      else framePos = (c == FRAME_HEAD[0]) ? 1 : 0;
      if (framePos == 1) frame[0] = c;
      continue;
    }
    frame[framePos++] = c;
    if (framePos == FRAME_LEN) {
      framePos = 0;
      if (frame[28] != FRAME_TAIL[0] || frame[29] != FRAME_TAIL[1]) continue;
      readBodiesFromFrame();
      lastFrameMs = millis();
      reportRoom();
    }
  }
}

int countBodies() {
  int n = 0;
  for (int i = 0; i < MAX_TARGETS; i++) if (bodies[i].present) n++;
  return n;
}

// Blue breathing = listening to an empty room, green = someone is playing,
// amber flash = the radar has stopped talking to us
void showMood() {
  uint32_t t = millis();
  float breath = 0.5f - 0.5f * cosf(t / 1000.0f * PI);
  switch (roomState) {
    case RADAR_SILENT:
      led.setPixelColor(0, (t / 250) % 2 ? led.Color(255, 120, 0) : 0);
      break;
    case ROOM_EMPTY:
      led.setPixelColor(0, led.Color(0, 0, 40 + 215 * breath));
      break;
    case ROOM_OCCUPIED: {
      uint8_t g = 80 + 175 * countBodies() / MAX_TARGETS;
      led.setPixelColor(0, led.Color(0, g, 0));
      break;
    }
  }
  led.show();
}

void setup() {
  Serial.begin(115200);  // USB CDC, baud is ignored
  radar.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
  Serial.printf("# room theremin v%s, LD2450 on RX%d TX%d\n", VERSION, RADAR_RX_PIN, RADAR_TX_PIN);
}

void loop() {
  listenToRadar();

  uint32_t now = millis();
  if (now - lastFrameMs > STALE_MS) {
    roomState = RADAR_SILENT;
    if (now - lastStaleMs > STALE_REPEAT_MS) {
      Serial.println("{\"stale\":1}");
      lastStaleMs = now;
    }
  } else {
    roomState = countBodies() ? ROOM_OCCUPIED : ROOM_EMPTY;
  }

  static uint32_t lastLedMs = 0;
  if (now - lastLedMs > 30) { showMood(); lastLedMs = now; }
}

/*
 * FLASH INSTRUCTIONS
 *   Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash 4MB
 *   Library: Adafruit NeoPixel
 *   Single node, no DEVICE_ID needed yet. When this joins the mesh it
 *   will get one.
 *   Test: open Serial Monitor, walk in front of the sensor, expect
 *   {"t":[[...]]} lines at ~10 Hz. Then close the monitor (only one
 *   program can hold the port) and open room_theremin.html in Chrome/Edge.
 */
