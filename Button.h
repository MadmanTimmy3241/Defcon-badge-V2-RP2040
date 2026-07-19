#pragma once
#include <Arduino.h>

// Debounced digital input shared by the Confirm/Purge buttons and the
// Silent Mode switch. Assumes active-LOW wiring: pin -> button/switch ->
// GND, using the RP2040's internal pull-up (no external resistor needed),
// so "active" means the pin reads LOW.
class DebouncedInput {
public:
  explicit DebouncedInput(uint8_t pin, uint16_t debounceMs = 25)
      : _pin(pin), _debounceMs(debounceMs) {}

  void begin() {
    pinMode(_pin, INPUT_PULLUP);
    _stable = _lastRaw = readActive();
    _lastChangeMs = millis();
  }

  // Call once per loop() iteration; timing depends on being called
  // regularly rather than on-demand.
  void update() {
    bool raw = readActive();
    if (raw != _lastRaw) {
      _lastRaw = raw;
      _lastChangeMs = millis();
    } else if (raw != _stable && (millis() - _lastChangeMs) >= _debounceMs) {
      bool wasActive = _stable;
      _stable = raw;
      if (!wasActive && _stable) _edge = true;
    }
  }

  // True exactly once per new press (inactive -> active transition);
  // consumes the edge so repeated calls don't re-fire it.
  bool pressed() {
    if (_edge) {
      _edge = false;
      return true;
    }
    return false;
  }

  // Current debounced level -- for the slide switch, not just buttons.
  bool isActive() const { return _stable; }

private:
  bool readActive() const { return digitalRead(_pin) == LOW; }

  uint8_t _pin;
  uint16_t _debounceMs;
  bool _stable = false;
  bool _lastRaw = false;
  uint32_t _lastChangeMs = 0;
  bool _edge = false;
};
