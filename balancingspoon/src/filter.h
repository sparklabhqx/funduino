#pragma once

// Complementary filter: integrates the gyro rate, corrected toward the
// accelerometer angle. alpha = 0.98 at 200 Hz.
class ComplementaryFilter {
 public:
  void reset(float angle_deg) { _angle = angle_deg; }

  float update(float accel_angle_deg, float gyro_rate_dps, float dt) {
    _angle = kAlpha * (_angle + gyro_rate_dps * dt) +
             (1.0f - kAlpha) * accel_angle_deg;
    return _angle;
  }

  float angle() const { return _angle; }

 private:
  static constexpr float kAlpha = 0.98f;
  float _angle = 0.0f;
};
