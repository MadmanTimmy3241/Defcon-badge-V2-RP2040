#pragma once
#include <Arduino.h>

// Thin wrapper around tone()/noTone() for the piezo buzzer (Same Sky
// CPT-1207-5LTH-T), wired directly from the GPIO to GND -- no driver
// transistor. It only draws ~2mA at its rated 5Vp-p, so the GPIO's 3.3V
// swing drives it fine on its own, and driving below its rated voltage is
// also what keeps the volume at a moderate level instead of its full
// rated 85dB. Chime frequencies below are chosen off the datasheet's
// resonant peak (~3-5kHz, the loudest/harshest part of its response
// curve) to stay gentle rather than sharp.
class Buzzer {
public:
  explicit Buzzer(uint8_t pin) : _pin(pin) {}

  void begin() {
    pinMode(_pin, OUTPUT);
    noTone(_pin);
  }

  // Gates all sound behind Silent Mode. Set from the slide switch's state
  // every loop() so flipping it takes effect immediately.
  void setMuted(bool muted) { _muted = muted; }

  void beep(uint16_t freqHz, uint16_t durationMs) {
    if (_muted) return;
    tone(_pin, freqHz, durationMs);
  }

  void confirmChime() { beep(2000, 80); }
  void purgeChime() { beep(400, 200); }

private:
  uint8_t _pin;
  bool _muted = false;
};
