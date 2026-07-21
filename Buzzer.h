#pragma once
#include <Arduino.h>

// One note in a non-blocking chime sequence -- see Buzzer::playSequence.
struct ChimeNote {
  uint16_t freqHz;
  uint16_t durationMs;
};

// Thin wrapper around tone()/noTone() for the piezo buzzer (Same Sky
// CPT-1207-5LTH-T), wired directly from the GPIO to GND -- no driver
// transistor. It only draws ~2mA at its rated 5Vp-p, so the GPIO's 3.3V
// swing drives it fine on its own, and driving below its rated voltage is
// also what keeps the volume at a moderate level instead of its full
// rated 85dB. Chime frequencies are chosen off the datasheet's resonant
// peak (~3-5kHz, the loudest/harshest part of its response curve) to stay
// gentle rather than sharp.
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
    _sequence = nullptr; // a one-shot beep overrides any running sequence
    tone(_pin, freqHz, durationMs);
  }

  void confirmChime() { beep(2000, 80); }

  // Plays a series of notes back-to-back without blocking loop() -- call
  // update() every loop() iteration to advance it. `notes` must outlive
  // the sequence (a static/global array, not a stack temporary).
  void playSequence(const ChimeNote *notes, uint8_t count) {
    if (_muted || count == 0) return;
    _sequence = notes;
    _seqLen = count;
    _seqIndex = 0;
    _seqNoteStart = millis();
    tone(_pin, _sequence[0].freqHz);
  }

  // Call every loop() iteration to advance any in-progress sequence.
  void update() {
    if (!_sequence) return;
    if (millis() - _seqNoteStart >= _sequence[_seqIndex].durationMs) {
      _seqIndex++;
      if (_seqIndex >= _seqLen) {
        noTone(_pin);
        _sequence = nullptr;
        return;
      }
      _seqNoteStart = millis();
      tone(_pin, _sequence[_seqIndex].freqHz);
    }
  }

private:
  uint8_t _pin;
  bool _muted = false;

  const ChimeNote *_sequence = nullptr;
  uint8_t _seqLen = 0;
  uint8_t _seqIndex = 0;
  uint32_t _seqNoteStart = 0;
};
