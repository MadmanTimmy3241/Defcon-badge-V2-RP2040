#pragma once
#include <Arduino.h>

// Thin wrapper around tone()/noTone() for the piezo buzzer (driven through
// an NPN switch transistor -- tone()'s normal GPIO toggling is exactly
// what a low-side switch transistor needs, no inversion required).
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
