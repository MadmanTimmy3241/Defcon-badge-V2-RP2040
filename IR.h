#pragma once
#include <Arduino.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include <pico/time.h>

// Shared NEC-style protocol timing. A packet is a 9-unit leader mark +
// 4.5-unit leader space (primes the TSOP's AGC), then 40 bits (32-bit
// badge ID + 8-bit checksum, MSB first) each encoded as a fixed-length
// mark followed by a short (0) or long (1) space, ending in one final
// stop mark. This is the same bit scheme NEC remotes use, chosen because
// TSOP38238 receivers are built and tuned for exactly this kind of
// 38kHz on/off-keyed signal.
constexpr uint32_t IR_CARRIER_HZ = 38000;
constexpr uint16_t IR_UNIT_US = 560;
constexpr uint16_t IR_LEADER_MARK_US = 9 * IR_UNIT_US;   // 5040
constexpr uint16_t IR_LEADER_SPACE_US = 2520;            // 4.5 units
constexpr uint16_t IR_BIT_MARK_US = IR_UNIT_US;          // 560
constexpr uint16_t IR_ZERO_SPACE_US = IR_UNIT_US;        // 560
constexpr uint16_t IR_ONE_SPACE_US = 3 * IR_UNIT_US;     // 1680
constexpr uint8_t IR_PACKET_BITS = 40;

inline uint8_t irChecksum(uint32_t payload) {
  return ((payload >> 24) & 0xFF) + ((payload >> 16) & 0xFF) +
         ((payload >> 8) & 0xFF) + (payload & 0xFF);
}

// Transmits a 32-bit payload as a NEC-style 38kHz burst. All bit timing
// runs off hardware alarms (pico-sdk add_alarm_in_us), not loop() polling
// -- necessary because other code in this sketch (the OLED's display())
// can block for tens of milliseconds, which would corrupt the
// sub-millisecond mark/space timing if this were advanced from loop()
// directly. The carrier itself free-runs on a PWM slice at ~33% duty
// (enough for TSOP38238 to detect, well under full LED current) and is
// gated on/off by switching the pin between the PWM peripheral and a
// plain GPIO-low drive -- more reliable than enabling/disabling the PWM
// slice, which freezes mid-waveform rather than deterministically low.
class IrTransmitter {
public:
  explicit IrTransmitter(uint8_t pin) : _pin(pin) {}

  void begin() {
    _slice = pwm_gpio_to_slice_num(_pin);
    uint32_t sysClk = clock_get_hz(clk_sys);
    _wrap = sysClk / IR_CARRIER_HZ;
    pwm_set_clkdiv(_slice, 1.0f);
    pwm_set_wrap(_slice, _wrap - 1);
    pwm_set_gpio_level(_pin, _wrap / 3); // ~33% duty -- plenty for TSOP,
                                         // meaningfully less LED current
                                         // than a full 50%+ duty carrier
    pwm_set_enabled(_slice, true);      // free-runs continuously in the
                                         // background; only actually
                                         // reaches the LED while carrierOn()
    carrierOff();
  }

  bool busy() const { return _state != State::Idle; }

  // Ignored while a send is already in progress.
  void send(uint32_t payload) {
    if (_state != State::Idle) return;
    uint8_t checksum = irChecksum(payload);
    _frame = (static_cast<uint64_t>(payload) << 8) | checksum;
    _bitIndex = 0;
    _state = State::LeaderMark;
    carrierOn();
    scheduleNext(IR_LEADER_MARK_US);
  }

private:
  enum class State : uint8_t { Idle, LeaderMark, LeaderSpace, BitMark, BitSpace, StopMark };

  void carrierOn() { gpio_set_function(_pin, GPIO_FUNC_PWM); }
  void carrierOff() {
    gpio_set_function(_pin, GPIO_FUNC_SIO);
    gpio_set_dir(_pin, GPIO_OUT);
    gpio_put(_pin, 0);
  }

  void scheduleNext(uint32_t delayUs) {
    add_alarm_in_us(delayUs, &IrTransmitter::alarmTrampoline, this, true);
  }

  static int64_t alarmTrampoline(alarm_id_t, void *ctx) {
    static_cast<IrTransmitter *>(ctx)->advance();
    return 0; // rescheduling is done explicitly in advance(), not via
              // the alarm's own repeat mechanism (delays vary per step)
  }

  // Runs in interrupt context -- keep it short, no blocking calls.
  void advance() {
    switch (_state) {
      case State::LeaderMark:
        carrierOff();
        _state = State::LeaderSpace;
        scheduleNext(IR_LEADER_SPACE_US);
        break;

      case State::LeaderSpace:
        carrierOn();
        _state = State::BitMark;
        scheduleNext(IR_BIT_MARK_US);
        break;

      case State::BitMark: {
        carrierOff();
        bool bit = (_frame >> (IR_PACKET_BITS - 1 - _bitIndex)) & 1;
        _state = State::BitSpace;
        scheduleNext(bit ? IR_ONE_SPACE_US : IR_ZERO_SPACE_US);
        break;
      }

      case State::BitSpace:
        _bitIndex++;
        carrierOn();
        if (_bitIndex < IR_PACKET_BITS) {
          _state = State::BitMark;
        } else {
          _state = State::StopMark;
        }
        scheduleNext(IR_BIT_MARK_US);
        break;

      case State::StopMark:
        carrierOff();
        _state = State::Idle;
        break;

      case State::Idle:
        break;
    }
  }

  uint8_t _pin;
  uint _slice = 0;
  uint32_t _wrap = 0;
  volatile State _state = State::Idle;
  uint64_t _frame = 0;
  volatile uint8_t _bitIndex = 0;
};

constexpr uint8_t IR_RX_BUF_SIZE = 128;

// Decodes a NEC-style 38kHz packet (see IrTransmitter) from a
// TSOP38238-style active-LOW receiver. Edge timestamps are captured in a
// GPIO interrupt -- precise regardless of what loop() is doing elsewhere
// -- and decoded from the buffered timestamps in update(), which just
// needs normal loop() cadence, not interrupt-level precision.
class IrReceiver {
public:
  explicit IrReceiver(uint8_t pin) : _pin(pin) {}

  void begin() {
    pinMode(_pin, INPUT);
    attachInterruptParam(digitalPinToInterrupt(_pin), isrTrampoline, CHANGE, this);
  }

  // True once per successfully decoded, checksum-valid packet; consumes
  // the result so it only fires once.
  bool available() {
    if (_packetReady) {
      _packetReady = false;
      return true;
    }
    return false;
  }

  uint32_t lastPayload() const { return _lastPayload; }

  // Call every loop() iteration to decode any buffered edges.
  void update() {
    uint8_t head = _head; // snapshot -- new edges during processing are
                           // left for the next update() call
    while (_tail != head) {
      uint32_t t = _buf[_tail];
      _tail = (_tail + 1) % IR_RX_BUF_SIZE;
      processEdge(t);
    }
  }

private:
  void processEdge(uint32_t timestamp) {
    uint32_t delta = timestamp - _lastEdge;
    _lastEdge = timestamp;

    if (!_gotLeader) {
      if (delta > 4000 && delta < 6500) _gotLeader = true;
      return;
    }
    if (!_leaderSpaceSeen) {
      if (delta > 2000 && delta < 3200) {
        _leaderSpaceSeen = true;
      } else {
        resetSync();
      }
      return;
    }

    // Alternating mark-end (ignored, fixed length) / space-end (decodes
    // the bit) edges from here on.
    _markSpaceToggle = !_markSpaceToggle;
    if (_markSpaceToggle) return; // this edge just ended a mark

    bool bit;
    if (delta > 350 && delta < 900) {
      bit = false;
    } else if (delta > 1300 && delta < 2100) {
      bit = true;
    } else {
      resetSync();
      return;
    }

    _frame = (_frame << 1) | (bit ? 1 : 0);
    _bitCount++;

    if (_bitCount == IR_PACKET_BITS) {
      uint32_t payload = static_cast<uint32_t>(_frame >> 8);
      uint8_t checksum = _frame & 0xFF;
      if (checksum == irChecksum(payload)) {
        _lastPayload = payload;
        _packetReady = true;
      }
      resetSync();
    }
  }

  void resetSync() {
    _gotLeader = false;
    _leaderSpaceSeen = false;
    _markSpaceToggle = false;
    _bitCount = 0;
    _frame = 0;
  }

  static void isrTrampoline(void *arg) {
    static_cast<IrReceiver *>(arg)->onEdge();
  }

  void onEdge() {
    uint32_t t = micros();
    uint8_t next = (_head + 1) % IR_RX_BUF_SIZE;
    if (next != _tail) { // drop the edge if the buffer's genuinely full
      _buf[_head] = t;
      _head = next;
    }
  }

  uint8_t _pin;
  volatile uint32_t _buf[IR_RX_BUF_SIZE];
  volatile uint8_t _head = 0;
  volatile uint8_t _tail = 0;

  uint32_t _lastEdge = 0;
  bool _gotLeader = false;
  bool _leaderSpaceSeen = false;
  bool _markSpaceToggle = false;
  uint8_t _bitCount = 0;
  uint64_t _frame = 0;

  volatile bool _packetReady = false;
  uint32_t _lastPayload = 0;
};
