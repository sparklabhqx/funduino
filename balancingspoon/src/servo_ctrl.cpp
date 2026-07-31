#include "servo_ctrl.h"

void ServoCtrl::begin(uint8_t pin) {
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  _servo.setPeriodHertz(50);
  // Default LEDC timer width is 10 bit = 19.5 us (~1.8 deg) per step, which
  // turns slow sweeps into a visible staircase. 16 bit gives 0.3 us steps.
  _servo.setTimerWidth(16);
  _servo.attach(pin, kMinUs, kMaxUs);
  _servo.writeMicroseconds(kCenterUs);
}

void ServoCtrl::setTarget(float deg) {
  _easing = false;
  deg = constrain(deg, -kTravelDeg, kTravelDeg);
  if (fabsf(deg - _last_applied_deg) < kDeadbandDeg) {
    return;  // inside the hysteresis band: hold the previous target
  }
  _last_applied_deg = deg;
  _target_deg = deg;
}

void ServoCtrl::easeToCenter() {
  _easing = true;
  _target_deg = 0.0f;
  _last_applied_deg = 1000.0f;  // next real command always passes the deadband
}

void ServoCtrl::update(float dt) {
  // Adaptive smoothing first (jitter-free at rest, near-zero lag when
  // moving), then a slew cap as the hard speed bound.
  const float smoothed = _smoother.filter(_target_deg, dt);

  const float rate = _easing ? kEaseDps : kSlewDps;
  const float max_step = rate * dt;
  const float err = smoothed - _current_deg;
  _current_deg += constrain(err, -max_step, max_step);

  float us = kCenterUs + _current_deg * kUsPerDeg;
  us = constrain(us, kCenterUs - kTravelDeg * kUsPerDeg,
                 kCenterUs + kTravelDeg * kUsPerDeg);
  const uint16_t new_us = (uint16_t)constrain(us, (float)kMinUs, (float)kMaxUs);
  if (new_us != _current_us) {  // don't disturb the PWM frame for no change
    _current_us = new_us;
    _servo.writeMicroseconds(_current_us);
  }
}
