// DEF CON 34 "Agency" badge - V2 (RP2040)
//
// This sketch currently sets up the local I2C1 bus (GP6/GP7) and drives a
// random, always-on scrolling "witty message" marquee across whichever
// displays are physically present. Handshake/IR/logging logic is not wired
// up yet.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_IS31FL3731.h>

#include "config.h"
#include "messages.h"
#include "ScrollingText.h"
#include "Button.h"
#include "Buzzer.h"

// ---------------------------------------------------------------------------
// Display drivers
// ---------------------------------------------------------------------------
// Only one OLED variant is ever wired up at a time (see config.h) -- both
// physical boards share the same factory-default address.
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire1, -1);
Adafruit_IS31FL3731_Wing matrixWing;

bool oledPresent = false;
bool matrixPresent = false;

// ---------------------------------------------------------------------------
// Marquees - one per display, all pulling from the same message pool
// ---------------------------------------------------------------------------
ScrollingText marqueeOled(oled, WITTY_MESSAGES, WITTY_MESSAGE_COUNT,
                           SSD1306_WHITE, OLED_TEXT_SIZE, /*pixelsPerSecond=*/125);
ScrollingText marqueeMatrix(matrixWing, WITTY_MESSAGES, WITTY_MESSAGE_COUNT,
                             /*brightness 0-255=*/60, /*textSize=*/1,
                             /*pixelsPerSecond=*/30);

// ---------------------------------------------------------------------------
// Buttons, Silent Mode switch, buzzer
// ---------------------------------------------------------------------------
// Declared before the diagnostic ticker below so nextDiagCard() can read
// swSilent's state for the SOUND card.
DebouncedInput btnConfirm(PIN_BTN_CONFIRM);
DebouncedInput btnLogPurge(PIN_BTN_LOG_PURGE);
DebouncedInput swSilent(PIN_SW_SILENT);
Buzzer buzzer(PIN_BUZZER);

// ---------------------------------------------------------------------------
// Diagnostic ticker - reserved top strip on the big OLED. Scrolls the
// opposite direction from the marquee below it so the two never sit at
// matching pixel columns, to avoid OLED burn-in.
// ---------------------------------------------------------------------------
// Not wired up to real IR receive logic yet -- placeholder until Passive
// Mode logging exists.
uint32_t badgesSeenCount = 0;

const char *nextDiagCard() {
  static char buf[24];
  static uint8_t card = 0;

  switch (card) {
    case 0: {
      uint32_t s = millis() / 1000;
      snprintf(buf, sizeof(buf), "UP %02u:%02u:%02u", (unsigned)(s / 3600),
               (unsigned)((s / 60) % 60), (unsigned)(s % 60));
      break;
    }
    case 1:
      snprintf(buf, sizeof(buf), "BADGES SEEN: %lu", badgesSeenCount);
      break;
    case 2:
    default:
      snprintf(buf, sizeof(buf), "SOUND: %s", swSilent.isActive() ? "OFF" : "ON");
      break;
  }
  card = (card + 1) % 3;
  return buf;
}

ScrollingText diagBand(oled, nextDiagCard, SSD1306_WHITE, /*textSize=*/1,
                        /*pixelsPerSecond=*/40);

// Adafruit_SSD1306::begin() doesn't reliably fail when no display is
// attached (the SSD1306 has no readable status register for BusIO to
// verify against), so we do our own raw ACK check first and only trust a
// driver's begin() once we know something actually answered at that
// address.
bool i2cDetect(uint8_t addr) {
  Wire1.beginTransmission(addr);
  return Wire1.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);

  Wire1.setSDA(PIN_I2C1_SDA);
  Wire1.setSCL(PIN_I2C1_SCL);
  Wire1.begin();
  Wire1.setClock(I2C1_CLOCK_HZ);

  randomSeed(rp2040.hwrand32());

  oledPresent = i2cDetect(I2C_ADDR_OLED) &&
                oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED);
  if (oledPresent) {
    oled.cp437(true);
    marqueeOled.setRegion(0, OLED_DIAG_HEIGHT, OLED_WIDTH, OLED_MARQUEE_HEIGHT);
    marqueeOled.begin();
    // No per-instance setFlush() here: with two regions sharing this one
    // display, each independently pushing the full framebuffer over I2C
    // staggers their timing against each other and looks jerky. loop()
    // combines both regions' updates into a single push instead.

    if (OLED_DIAG_HEIGHT > 0) {
      diagBand.setRegion(0, 0, OLED_WIDTH, OLED_DIAG_HEIGHT);
      diagBand.setDirection(ScrollDirection::LeftToRight);
      diagBand.begin();
    }
  }
  Serial.printf("%dx%d OLED @0x%02X: %s\n", OLED_WIDTH, OLED_HEIGHT,
                I2C_ADDR_OLED, oledPresent ? "found" : "not found");

  matrixPresent = i2cDetect(I2C_ADDR_MATRIX_WING) &&
                  matrixWing.begin(I2C_ADDR_MATRIX_WING, &Wire1);
  if (matrixPresent) {
    marqueeMatrix.begin();
    marqueeMatrix.setClear([]() { matrixWing.clear(); });
  }
  Serial.printf("15x7 Matrix Wing @0x%02X: %s\n", I2C_ADDR_MATRIX_WING,
                matrixPresent ? "found" : "not found");

  btnConfirm.begin();
  btnLogPurge.begin();
  swSilent.begin();
  buzzer.begin();
}

void loop() {
  if (oledPresent) {
    bool changed = marqueeOled.update();
    if (OLED_DIAG_HEIGHT > 0) changed |= diagBand.update();
    if (changed) oled.display();
  }
  if (matrixPresent) marqueeMatrix.update();

  btnConfirm.update();
  btnLogPurge.update();
  swSilent.update();
  buzzer.setMuted(swSilent.isActive());

  if (btnConfirm.pressed()) {
    buzzer.confirmChime();
    Serial.println(F("Confirm button pressed"));
    // TODO: feed into Handshake Mode's mutual-confirm logic once IR
    // alignment detection exists.
  }

  if (btnLogPurge.pressed()) {
    buzzer.purgeChime();
    badgesSeenCount = 0; // placeholder until persistent flash log exists
    Serial.println(F("Log purge button pressed - badgesSeenCount reset"));
    // TODO: purge the persistent flash log once Passive Mode logging exists.
  }

  if (Serial.available()) {
    while (Serial.available()) Serial.read(); // drain whatever was sent
    runDiagnostics();
  }
}

// Send any byte (e.g. hit Enter in the Serial Monitor) to trigger this.
void runDiagnostics() {
  Serial.println(F("--- diagnostics ---"));
  Serial.printf("built: %s %s\n", __DATE__, __TIME__);
  Serial.printf("i2c1 clock: %lu Hz\n", I2C1_CLOCK_HZ);
  Serial.printf("uptime: %lu ms\n", millis());
  Serial.printf("free heap: %u bytes\n", rp2040.getFreeHeap());

  Serial.println(F("displays detected at boot:"));
  Serial.printf("  %dx%d OLED @0x%02X: %s\n", OLED_WIDTH, OLED_HEIGHT,
                I2C_ADDR_OLED, oledPresent ? "present" : "absent");
  Serial.printf("  15x7 Matrix @0x%02X: %s\n", I2C_ADDR_MATRIX_WING,
                matrixPresent ? "present" : "absent");

  Serial.println(F("inputs:"));
  Serial.printf("  silent mode switch: %s\n", swSilent.isActive() ? "ON" : "off");
  Serial.printf("  confirm button: %s\n", btnConfirm.isActive() ? "held" : "released");
  Serial.printf("  log purge button: %s\n", btnLogPurge.isActive() ? "held" : "released");

  Serial.println(F("live I2C1 bus scan:"));
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0) {
      Serial.printf("  device @0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println(F("  (no devices responded)"));
  Serial.println(F("-------------------"));
}
