#pragma once
#include <Adafruit_GFX.h>
#include <math.h>

// Draws a 5-pointed star (connecting every 2nd vertex of a pentagon --
// the classic pentagram construction) at the given rotation. Shared by
// PentacleAnimation's full-screen spin and the small static "you've seen
// this badge before" icon drawn once in the diagnostic band.
inline void drawPentagram(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t radius,
                          float rotationRad, uint16_t color) {
  float x[5], y[5];
  for (uint8_t i = 0; i < 5; i++) {
    float a = rotationRad + i * (2 * PI / 5) - PI / 2; // start pointing up
    x[i] = cx + radius * cosf(a);
    y[i] = cy + radius * sinf(a);
  }
  // Connect each point to the one two steps away, not its immediate
  // neighbor -- that's what makes the star shape instead of a plain
  // pentagon.
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t j = (i + 2) % 5;
    gfx.drawLine(static_cast<int16_t>(x[i]), static_cast<int16_t>(y[i]),
                 static_cast<int16_t>(x[j]), static_cast<int16_t>(y[j]), color);
  }
}

// Full-screen rotating pentagram takeover -- the "you just saw a reserved
// badge ID" easter egg. Spins through one full rotation over its
// duration inside a circle. Owns the whole display while active();
// caller must not also draw other content on top until it's finished
// (see V2-RP2040.ino's loop()).
class PentacleAnimation {
public:
  void begin(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    _gfx = &gfx;
    _cx = cx;
    _cy = cy;
    _radius = radius;
    _color = color;
  }

  void start() {
    _active = true;
    _startMs = millis();
  }

  bool active() const { return _active; }

  // Renders the current frame and returns true while still active. On
  // the call where the duration elapses, deactivates and returns false
  // instead of rendering -- lets the caller fall through to its normal
  // display content in that same loop() iteration, no dead frame.
  bool update() {
    if (!_active) return false;
    uint32_t elapsed = millis() - _startMs;
    if (elapsed >= DURATION_MS) {
      _active = false;
      return false;
    }
    render(elapsed);
    return true;
  }

private:
  static constexpr uint32_t DURATION_MS = 3000;

  void render(uint32_t elapsed) {
    _gfx->fillScreen(0);
    float baseAngle = (static_cast<float>(elapsed) / DURATION_MS) * 2 * PI;
    drawPentagram(*_gfx, _cx, _cy, _radius, baseAngle, _color);
    _gfx->drawCircle(_cx, _cy, _radius + 4, _color);
  }

  Adafruit_GFX *_gfx = nullptr;
  int16_t _cx = 0;
  int16_t _cy = 0;
  int16_t _radius = 0;
  uint16_t _color = 1;
  bool _active = false;
  uint32_t _startMs = 0;
};
