#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>

// Adaptive smoother (One Euro filter): cutoff rises with signal speed, so a
// still signal gets heavy smoothing (no jitter) while a fast tilt passes
// through almost unfiltered (no lag).
struct OneEuroFilter {
  static constexpr float kMinCutoffHz = 1.5f;  // smoothing at rest
  static constexpr float kBeta = 0.15f;        // cutoff gain vs. speed
  static constexpr float kDCutoffHz = 1.5f;    // derivative smoothing

  float x = 0.0f, dx = 0.0f;

  static float alphaFor(float cutoff_hz, float dt) {
    const float tau = 1.0f / (2.0f * PI * cutoff_hz);
    return dt / (tau + dt);
  }

  void reset(float v) { x = v; dx = 0.0f; }

  float filter(float v, float dt) {
    const float dx_raw = (v - x) / dt;
    dx += alphaFor(kDCutoffHz, dt) * (dx_raw - dx);
    const float cutoff = kMinCutoffHz + kBeta * fabsf(dx);
    x += alphaFor(cutoff, dt) * (v - x);
    return x;
  }
};

// Servo output stage: travel clamp, hysteresis deadband, adaptive smoothing,
// slew limiting, pulse-width mapping. Commands are in "spoon degrees"
// relative to center.
class ServoCtrl {
 public:
  void begin(uint8_t pin);

  // Normal control path: clamps to +/-60 deg, applies the 0.3 deg
  // hysteresis deadband against chatter.
  void setTarget(float deg);

  // Failsafe path: glide to center at a gentle rate, bypassing the deadband.
  void easeToCenter();

  // Call every loop tick; slews the output toward the target and writes
  // the pulse. dt in seconds.
  void update(float dt);

  float currentDeg() const { return _current_deg; }
  uint16_t currentUs() const { return _current_us; }

 private:
  static constexpr uint16_t kMinUs = 500;
  static constexpr uint16_t kMaxUs = 2500;
  static constexpr uint16_t kCenterUs = 1500;
  static constexpr float kUsPerDeg = (kMaxUs - kMinUs) / 180.0f;  // ~11.1
  static constexpr float kTravelDeg = 60.0f;   // software travel limit
  static constexpr float kSlewDps = 300.0f;    // normal slew limit
  static constexpr float kEaseDps = 80.0f;     // failsafe glide rate
  // Kept tiny: real chatter suppression is the One Euro smoother's job; a
  // large deadband here causes stick-slip (no motion, then a lurch).
  static constexpr float kDeadbandDeg = 0.05f;

  Servo _servo;
  OneEuroFilter _smoother;
  float _target_deg = 0.0f;
  float _current_deg = 0.0f;
  float _last_applied_deg = 1000.0f;  // far away so the first command passes
  uint16_t _current_us = kCenterUs;
  bool _easing = false;
};
