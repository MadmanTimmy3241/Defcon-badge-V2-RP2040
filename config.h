#pragma once

// ---------------------------------------------------------------------------
// Pin mapping
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_IR_OMNI_TX    = 2;  // Omni IR TX -> 4x edge LEDs (NPN)
constexpr uint8_t PIN_IR_OMNI_RX    = 3;  // Omni IR RX <- 4x TSOP38238, diode-OR'd, active LOW
constexpr uint8_t PIN_IR_DIR_TX     = 4;  // Directional/handshake IR TX -> front LED (NPN)
constexpr uint8_t PIN_IR_DIR_RX     = 5;  // Directional/handshake IR RX <- front TSOP38238
constexpr uint8_t PIN_I2C1_SDA      = 6;  // I2C1 SDA (local bus)
constexpr uint8_t PIN_I2C1_SCL      = 7;  // I2C1 SCL (local bus)
constexpr uint8_t PIN_BUZZER        = 8;  // Piezo buzzer (PWM/tone, direct-drive)
constexpr uint8_t PIN_BTN_CONFIRM   = 9;  // Handshake Confirm button
constexpr uint8_t PIN_BTN_LOG_PURGE = 10; // Log Purge button
constexpr uint8_t PIN_SW_SILENT     = 11; // Silent Mode slide switch

// Buttons and the slide switch are read via the RP2040's internal
// pull-ups (see Button.h / DebouncedInput): wire each one straight
// between its GPIO and GND, no external resistor needed. Pressed/on
// reads as LOW at the pin.

// ---------------------------------------------------------------------------
// I2C1 bus (GP6/GP7) - 7-bit addresses as seen by Wire
// ---------------------------------------------------------------------------
constexpr uint8_t I2C_ADDR_MATRIX_WING = 0x74; // Adafruit 15x7 Charlieplex Wing (IS31FL3731)

// Only one OLED is ever wired up at a time, and both boards sit at the same
// factory-default address (0x3C), so there's no need to move either jumper.
// Since the two boards are indistinguishable on the bus (same address,
// no readable chip ID), pick which physical one is currently connected
// here so the code knows what resolution to drive.
#define OLED_VARIANT_BIG 1 // 128x64 SSD1306
// #define OLED_VARIANT_GROVE 1 // 64x48 SSD1306 -- comment out BIG and uncomment this instead

#if defined(OLED_VARIANT_BIG) && defined(OLED_VARIANT_GROVE)
#error "Select only one OLED_VARIANT_* in config.h -- only one OLED is ever connected at a time"
#endif

constexpr uint8_t I2C_ADDR_OLED = 0x3C; // factory default on both boards

#if defined(OLED_VARIANT_BIG)
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
// This panel is physically two-tone (top strip glows yellow, rest glows
// blue). Reserve the yellow strip for a status readout and keep the
// marquee confined to the blue area below it.
constexpr uint8_t OLED_DIAG_HEIGHT = 16;
#elif defined(OLED_VARIANT_GROVE)
constexpr uint8_t OLED_WIDTH = 64;
constexpr uint8_t OLED_HEIGHT = 48;
constexpr uint8_t OLED_DIAG_HEIGHT = 0; // single-color panel, no split needed
#else
#error "Select an OLED_VARIANT_* in config.h"
#endif

constexpr uint8_t OLED_MARQUEE_HEIGHT = OLED_HEIGHT - OLED_DIAG_HEIGHT;

// Largest integer size whose 8px-per-line glyph height still fits the
// marquee area without clipping.
constexpr uint8_t OLED_TEXT_SIZE = OLED_MARQUEE_HEIGHT / 8;

constexpr uint32_t I2C1_CLOCK_HZ = 400000; // Fast mode; wiring is soldered and verified good
