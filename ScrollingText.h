#pragma once
#include <Adafruit_GFX.h>
#include <functional>
#include <stdint.h>

enum class ScrollDirection : uint8_t { RightToLeft, LeftToRight };

// Drives a horizontally scrolling message on any Adafruit_GFX-backed
// display (SSD1306 OLEDs, IS31FL3731 matrix, etc). Non-blocking: call
// update() every loop() iteration, it self-throttles based on
// pixelsPerSecond and only draws a new frame when the scroll position has
// actually advanced by a whole pixel.
//
// Two content modes:
//  - Fixed pool: pass an array of strings, a random one (never repeating
//    the previous pick) plays each time the current one scrolls off.
//  - Provider: pass a callback returning the next string to show, called
//    once per card in the order it's called -- for live-formatted content
//    (uptime, counters, etc) that a fixed pool can't express.
class ScrollingText {
public:
  ScrollingText(Adafruit_GFX &gfx, const char *const *messages,
                size_t messageCount, uint16_t textColor,
                uint8_t textSize = 1, uint16_t pixelsPerSecond = 40)
      : _gfx(gfx), _messages(messages), _messageCount(messageCount),
        _color(textColor), _textSize(textSize), _speed(pixelsPerSecond) {}

  ScrollingText(Adafruit_GFX &gfx, std::function<const char *()> provider,
                uint16_t textColor, uint8_t textSize = 1,
                uint16_t pixelsPerSecond = 40)
      : _gfx(gfx), _provider(provider), _color(textColor),
        _textSize(textSize), _speed(pixelsPerSecond) {}

  // Confines the marquee to a sub-rectangle of the display instead of the
  // full screen -- e.g. to share a panel with a separately-drawn status
  // area. Call before begin(). Defaults to the full screen if never called.
  void setRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
    _regionX = x;
    _regionY = y;
    _width = w;
    _height = h;
    _hasRegion = true;
  }

  // Call before begin(). Defaults to RightToLeft.
  void setDirection(ScrollDirection direction) { _direction = direction; }

  // Called once, after the underlying driver's own begin()/init.
  void begin() {
    if (!_hasRegion) {
      _width = _gfx.width();
      _height = _gfx.height();
    }
    _gfx.setTextWrap(false);
    pickNextMessage();
    _x = (_direction == ScrollDirection::RightToLeft) ? _width : -_textWidthPx;
    _lastStepUs = micros();
  }

  // Called after each drawn frame to push it to the panel, e.g.
  // [&]{ oled.display(); } for SSD1306. Leave unset for displays (like the
  // IS31FL3731) whose drawPixel() writes straight to the chip.
  void setFlush(std::function<void()> flushFn) { _flush = flushFn; }

  // Called instead of gfx.fillRect(region, 0) to erase the previous frame.
  // Override if the concrete driver has a cheaper/bulk clear (e.g. the
  // IS31FL3731's clear(), which is fewer I2C transactions than per-pixel
  // fillRect on a 105-LED matrix).
  void setClear(std::function<void()> clearFn) { _clear = clearFn; }

  // Returns true if a new frame was actually drawn this call, so callers
  // sharing a display across multiple ScrollingText instances can combine
  // several regions' updates into a single flush/push instead of each
  // instance pushing independently (which, on a slow bus, staggers their
  // timing against each other and shows up as uneven/jerky motion).
  bool update() {
    uint32_t now = micros();
    uint32_t elapsedUs = now - _lastStepUs;
    int32_t deltaPx = static_cast<int32_t>((elapsedUs / 1000000.0f) * _speed);
    if (deltaPx <= 0) return false;
    _lastStepUs = now;

    if (_direction == ScrollDirection::RightToLeft) {
      _x -= deltaPx;
      if (_x < -_textWidthPx) {
        pickNextMessage();
        _x = _width;
      }
    } else {
      _x += deltaPx;
      if (_x > _width) {
        pickNextMessage();
        _x = -_textWidthPx;
      }
    }

    render();
    return true;
  }

private:
  void pickNextMessage() {
    if (_provider) {
      _current = _provider();
    } else {
      if (_messageCount == 0) return;

      size_t idx;
      if (_messageCount == 1) {
        idx = 0;
      } else {
        do {
          idx = random(_messageCount);
        } while (idx == _lastIndex);
      }
      _lastIndex = idx;
      _current = _messages[idx];
    }

    int16_t x1, y1;
    uint16_t w, h;
    _gfx.setTextSize(_textSize);
    _gfx.getTextBounds(_current, 0, 0, &x1, &y1, &w, &h);
    _textWidthPx = static_cast<int32_t>(w);
    _textHeightPx = static_cast<int32_t>(h);
  }

  void render() {
    if (_clear) {
      _clear();
    } else {
      _gfx.fillRect(_regionX, _regionY, _width, _height, 0);
    }

    _gfx.setTextSize(_textSize);
    _gfx.setTextColor(_color);
    int16_t y = _regionY + static_cast<int16_t>((_height - _textHeightPx) / 2);
    if (y < _regionY) y = _regionY;
    _gfx.setCursor(static_cast<int16_t>(_regionX + _x), y);
    _gfx.print(_current);

    if (_flush) _flush();
  }

  Adafruit_GFX &_gfx;
  const char *const *_messages = nullptr;
  size_t _messageCount = 0;
  std::function<const char *()> _provider;
  uint16_t _color;
  uint8_t _textSize;
  uint16_t _speed; // pixels per second
  ScrollDirection _direction = ScrollDirection::RightToLeft;

  const char *_current = nullptr;
  size_t _lastIndex = SIZE_MAX;
  int32_t _x = 0;
  int32_t _textWidthPx = 0;
  int32_t _textHeightPx = 0;
  int16_t _regionX = 0;
  int16_t _regionY = 0;
  int16_t _width = 0;
  int16_t _height = 0;
  bool _hasRegion = false;
  uint32_t _lastStepUs = 0;

  std::function<void()> _flush;
  std::function<void()> _clear;
};
