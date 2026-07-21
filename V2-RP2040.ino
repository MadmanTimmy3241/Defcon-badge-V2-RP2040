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
#include "IR.h"
#include "Pentacle.h"

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

// Log Purge requires a deliberate hold before it actually clears anything,
// so a brief accidental bump can't wipe the log. The warning chime plays
// the moment the hold begins (a chance to let go if it wasn't
// intentional); the confirm chime plays only once the hold completes and
// the purge actually fires.
constexpr uint32_t PURGE_HOLD_MS = 3000;
const ChimeNote PURGE_WARNING[] = {{2200, 150}, {700, 150}, {2200, 150}};
const ChimeNote PURGE_CONFIRM[] = {{1200, 90}, {1600, 90}, {2200, 140}};

// ---------------------------------------------------------------------------
// IR - omni ring (Passive Mode broadcast/receive) + directional handshake
// pair. Directional TX is intentionally never called yet -- it's only
// meant to transmit while actively probing for a face-to-face partner,
// which is Handshake Mode logic that doesn't exist yet. Its receiver is
// still wired up so we can confirm that pin decodes correctly too.
// ---------------------------------------------------------------------------
IrTransmitter irOmniTx(PIN_IR_OMNI_TX);
IrReceiver irOmniRx(PIN_IR_OMNI_RX);
IrTransmitter irDirTx(PIN_IR_DIR_TX);
IrReceiver irDirRx(PIN_IR_DIR_RX);

uint32_t myBadgeId = 0;
constexpr uint32_t OMNI_BROADCAST_INTERVAL_MS = 700;

// ---------------------------------------------------------------------------
// Easter egg - seeing EASTER_EGG_BADGE_ID over IR takes over the whole
// OLED with a spinning pentagram and an ominous stinger chime, but only
// the first time. After that, no repeat animation/sound -- just a small
// static star left permanently in a reserved corner of the diagnostic
// band, so it doesn't get spammed on every re-sighting of the same badge.
// ---------------------------------------------------------------------------
PentacleAnimation pentacle;
const ChimeNote EASTER_EGG_STINGER[] = {{600, 150}, {500, 150}, {350, 300}};
bool easterEggTriggered = false;

constexpr int16_t EASTER_EGG_STAR_MARGIN = 16; // reserved column width, right edge of the diag band
constexpr int16_t EASTER_EGG_STAR_RADIUS = 5;

void playEasterEggEffect() {
  pentacle.start();
  buzzer.playSequence(EASTER_EGG_STINGER, 3);
}

// Seeing EASTER_EGG_BADGE_ID over IR -- one-time, grants the permanent
// star.
void triggerEasterEgg() {
  if (easterEggTriggered) return; // already seen -- no repeat
  easterEggTriggered = true;
  playEasterEggEffect();
}

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
  myBadgeId = (FORCE_BADGE_ID != 0) ? FORCE_BADGE_ID : rp2040.hwrand32();

  oledPresent = i2cDetect(I2C_ADDR_OLED) &&
                oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED);
  if (oledPresent) {
    oled.cp437(true);
    // Adafruit_SSD1306::begin() draws its own splash logo into the buffer
    // by default; clear it so no stray lit pixels linger in regions nothing
    // else ever redraws (e.g. the easter egg's reserved star column).
    oled.clearDisplay();
    oled.display();
    marqueeOled.setRegion(0, OLED_DIAG_HEIGHT, OLED_WIDTH, OLED_MARQUEE_HEIGHT);
    marqueeOled.begin();
    // No per-instance setFlush() here: with two regions sharing this one
    // display, each independently pushing the full framebuffer over I2C
    // staggers their timing against each other and looks jerky. loop()
    // combines both regions' updates into a single push instead.

    if (OLED_DIAG_HEIGHT > 0) {
      // Right edge is reserved for the easter egg's star icon (see
      // triggerEasterEgg()) so its own fillRect-based clear never wipes it.
      diagBand.setRegion(0, 0, OLED_WIDTH - EASTER_EGG_STAR_MARGIN, OLED_DIAG_HEIGHT);
      diagBand.setDirection(ScrollDirection::LeftToRight);
      diagBand.begin();
    }

    int16_t pentacleRadius = (min(OLED_WIDTH, OLED_HEIGHT) / 2) - 8;
    pentacle.begin(oled, OLED_WIDTH / 2, OLED_HEIGHT / 2, pentacleRadius, SSD1306_WHITE);
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

  irOmniTx.begin();
  irOmniRx.begin();
  irDirTx.begin();
  irDirRx.begin();
  Serial.printf("badge ID: 0x%08lX\n", myBadgeId);
}

void loop() {
  if (oledPresent) {
    bool changed = pentacle.update();
    if (!pentacle.active()) {
      // Either the pentacle was never running, or this is the exact
      // iteration it just finished on -- either way, normal content owns
      // the screen again immediately, no dead frame in between.
      changed |= marqueeOled.update();
      if (OLED_DIAG_HEIGHT > 0) {
        bool diagChanged = diagBand.update();
        changed |= diagChanged;
        if (diagChanged) {
          // diagBand's own clear only covers its logical region width,
          // but text drawing isn't clipped to that boundary -- as a
          // message scrolls in/out, characters can visually spill past
          // it into this reserved column. Reclaim the column every time
          // diagBand actually redraws (not just once), so spillover never
          // accumulates, then redraw the star on top if it's been earned.
          oled.fillRect(OLED_WIDTH - EASTER_EGG_STAR_MARGIN, 0,
                        EASTER_EGG_STAR_MARGIN, OLED_DIAG_HEIGHT, 0);
          if (easterEggTriggered) {
            drawPentagram(oled, OLED_WIDTH - EASTER_EGG_STAR_MARGIN / 2, OLED_DIAG_HEIGHT / 2,
                          EASTER_EGG_STAR_RADIUS, 0.0f, SSD1306_WHITE);
          }
        }
      }
    }
    if (changed) oled.display();
  }
  if (matrixPresent) marqueeMatrix.update();

  btnConfirm.update();
  btnLogPurge.update();
  swSilent.update();
  buzzer.setMuted(swSilent.isActive());
  buzzer.update();

  if (btnConfirm.pressed()) {
    buzzer.confirmChime();
    Serial.println(F("Confirm button pressed"));
    // TODO: feed into Handshake Mode's mutual-confirm logic once IR
    // alignment detection exists.
  }

  // Secret manual trigger: Silent Mode engaged + both buttons held
  // together plays the same animation/sound as spotting the reserved
  // badge ID, but deliberately does NOT grant the permanent star -- that
  // stays reserved for an actual IR sighting.
  static bool secretComboActive = false;
  bool secretComboNow = swSilent.isActive() && btnConfirm.isActive() && btnLogPurge.isActive();
  if (secretComboNow && !secretComboActive) {
    playEasterEggEffect();
    Serial.println(F("Secret combo triggered (Silent + both buttons)"));
  }
  secretComboActive = secretComboNow;

  static uint32_t purgeHoldStart = 0;
  static bool purgeHolding = false;
  static bool purgeFired = false;

  if (btnLogPurge.pressed()) {
    purgeHoldStart = millis();
    purgeHolding = true;
    purgeFired = false;
    buzzer.playSequence(PURGE_WARNING, 3);
  }

  if (purgeHolding) {
    if (!btnLogPurge.isActive()) {
      // Released before the hold completed -- cancel, no purge.
      purgeHolding = false;
      if (!purgeFired) Serial.println(F("Log purge cancelled - released early"));
    } else if (!purgeFired && (millis() - purgeHoldStart >= PURGE_HOLD_MS)) {
      badgesSeenCount = 0; // placeholder until persistent flash log exists
      buzzer.playSequence(PURGE_CONFIRM, 3);
      Serial.println(F("Log purge confirmed (held 3s) - badgesSeenCount reset"));
      // TODO: purge the persistent flash log once Passive Mode logging exists.
      purgeFired = true;
    }
  }

  static uint32_t lastOmniBroadcast = 0;
  if (!irOmniTx.busy() && (millis() - lastOmniBroadcast >= OMNI_BROADCAST_INTERVAL_MS)) {
    irOmniTx.send(myBadgeId);
    lastOmniBroadcast = millis();
  }

  irOmniRx.update();
  irDirRx.update();

  if (irOmniRx.available()) {
    uint32_t id = irOmniRx.lastPayload();
    badgesSeenCount++; // placeholder count -- no dedup/flash logging yet
    Serial.printf("omni RX: badge 0x%08lX (seen count now %lu)\n", id, badgesSeenCount);
    if (id == EASTER_EGG_BADGE_ID) triggerEasterEgg();
  }
  if (irDirRx.available()) {
    uint32_t id = irDirRx.lastPayload();
    Serial.printf("directional RX: badge 0x%08lX\n", id);
    if (id == EASTER_EGG_BADGE_ID) triggerEasterEgg();
    // TODO: feed into Handshake Mode's alignment/probe logic once it exists.
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

  Serial.println(F("IR:"));
  Serial.printf("  badge ID: 0x%08lX\n", myBadgeId);
  Serial.printf("  badges seen (unlogged count): %lu\n", badgesSeenCount);

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
