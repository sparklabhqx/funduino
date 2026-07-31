// One-axis self-balancing spoon.
// MPU-6050 measures base tilt; a micro servo counter-rotates the spoon clamp
// so the spoon stays level while the base tilts around the servo shaft axis.
//
// ESP32 WROOM-32 DevKit: I2C SDA=25 SCL=26, servo signal GPIO27, LED GPIO2.

#include <Arduino.h>

#include "cli.h"
#include "config.h"
#include "filter.h"
#include "imu.h"
#include "servo_ctrl.h"

static const uint8_t PIN_SDA = 25;
static const uint8_t PIN_SCL = 26;
static const uint8_t PIN_SERVO = 27;
static const uint8_t PIN_LED = 2;
static const uint8_t MPU_ADDR = 0x68;

static const uint32_t kTickUs = 5000;         // 200 Hz control loop
static const float kNominalDt = 0.005f;
static const uint32_t kCalDurationMs = 2000;  // gyro bias window
static const uint32_t kCalBlinkMs = 100;
static const uint32_t kImuTimeoutMs = 100;    // failsafe trip point
static const uint32_t kReinitEveryMs = 500;   // I2C re-setup cadence in failsafe
static const uint32_t kFailBlinkMs = 50;
static const uint32_t kTelemetryMs = 50;      // 20 Hz

enum class State { CALIBRATING, RUNNING, FAILSAFE };

static Imu imu;
static ComplementaryFilter filter;
static ServoCtrl servo;
static Cli cli;
static Config cfg;

static State state = State::CALIBRATING;
static bool telemetry = false;

static uint32_t next_tick_us = 0;
static uint32_t last_tick_us = 0;

// Calibration accumulators
static uint32_t cal_start_ms = 0;
static float cal_sum_gx = 0, cal_sum_gy = 0;
static uint32_t cal_count = 0;
static float bias_gx = 0, bias_gy = 0;

static uint32_t last_imu_ok_ms = 0;
static uint32_t last_reinit_ms = 0;
static uint32_t last_blink_ms = 0;
static uint32_t last_telemetry_ms = 0;
static bool reseed_filter = false;

static float last_accel_angle = 0;  // raw (accel-only) angle, for telemetry

// Tilt of the selected axis from the accelerometer, in degrees.
static float accelAngle(const ImuSample &s) {
  if (cfg.axis == 0) {
    return atan2f(s.ay, s.az) * RAD_TO_DEG;  // roll about X
  }
  return atan2f(-s.ax, sqrtf(s.ay * s.ay + s.az * s.az)) * RAD_TO_DEG;  // pitch about Y
}

static float gyroRate(const ImuSample &s) {
  return (cfg.axis == 0) ? (s.gx - bias_gx) : (s.gy - bias_gy);
}

static void startCalibration() {
  state = State::CALIBRATING;
  cal_start_ms = millis();
  cal_sum_gx = cal_sum_gy = 0;
  cal_count = 0;
  servo.easeToCenter();
}

// --- CLI actions -----------------------------------------------------------

static void onCaptureLevel() {
  cfg.level_deg = filter.angle();
  Serial.printf("level captured: %.2f deg\n", cfg.level_deg);
}

static void onAxisToggle() {
  cfg.axis = cfg.axis == 0 ? 1 : 0;
  reseed_filter = true;  // restart the estimate from the accel on the new axis
}

static void onSave() {
  configSave(cfg);
  Serial.println("config saved to NVS");
}

// --- Arduino entry points --------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  configLoad(cfg);
  servo.begin(PIN_SERVO);
  cli.begin(&cfg, &telemetry, onCaptureLevel, onAxisToggle, onSave);

  Serial.println("\n=== balance spoon ===");
  Serial.printf("gain %.2f  trim %.2f  level %.2f  invert %s  axis %s\n",
                cfg.gain, cfg.trim_deg, cfg.level_deg,
                cfg.invert ? "on" : "off", cfg.axis == 0 ? "X" : "Y");
  cli.printHelp();

  if (!imu.begin(PIN_SDA, PIN_SCL, MPU_ADDR)) {
    Serial.println("! MPU-6050 not responding, retrying in background");
    state = State::FAILSAFE;
  } else {
    Serial.println("calibrating gyro, keep the device still...");
    startCalibration();
  }
  last_imu_ok_ms = millis();
  next_tick_us = micros() + kTickUs;
  last_tick_us = micros();
}

void loop() {
  cli.poll();

  const uint32_t now_us = micros();
  if ((int32_t)(now_us - next_tick_us) < 0) {
    return;  // not yet time for the next 200 Hz tick
  }
  next_tick_us += kTickUs;
  if ((int32_t)(now_us - next_tick_us) > (int32_t)(4 * kTickUs)) {
    next_tick_us = now_us + kTickUs;  // fell far behind; resync
  }

  float dt = (now_us - last_tick_us) * 1e-6f;
  last_tick_us = now_us;
  if (dt <= 0.0f || dt > 0.05f) dt = kNominalDt;

  const uint32_t now_ms = millis();
  ImuSample s;
  const bool imu_ok = imu.read(s);
  if (imu_ok) {
    last_imu_ok_ms = now_ms;
  }

  switch (state) {
    case State::CALIBRATING: {
      if (imu_ok) {
        cal_sum_gx += s.gx;
        cal_sum_gy += s.gy;
        cal_count++;
      }
      if (now_ms - last_blink_ms >= kCalBlinkMs) {
        last_blink_ms = now_ms;
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      }
      if (now_ms - cal_start_ms >= kCalDurationMs) {
        if (cal_count < 100) {  // not enough samples: sensor flaky, try again
          Serial.println("! calibration got too few samples, restarting");
          imu.reinit();
          startCalibration();
          break;
        }
        bias_gx = cal_sum_gx / cal_count;
        bias_gy = cal_sum_gy / cal_count;
        filter.reset(accelAngle(s));
        state = State::RUNNING;
        digitalWrite(PIN_LED, HIGH);
        Serial.printf("calibrated: bias gx %.2f gy %.2f dps (%lu samples)\n",
                      bias_gx, bias_gy, (unsigned long)cal_count);
      }
      break;
    }

    case State::RUNNING: {
      if (imu_ok) {
        last_accel_angle = accelAngle(s);
        if (reseed_filter) {
          reseed_filter = false;
          filter.reset(last_accel_angle);
        }
        filter.update(last_accel_angle, gyroRate(s), dt);

        const float error = filter.angle() - cfg.level_deg + cfg.trim_deg;
        const float sign = cfg.invert ? -1.0f : 1.0f;
        servo.setTarget(sign * cfg.gain * error);
      } else if (now_ms - last_imu_ok_ms > kImuTimeoutMs) {
        Serial.println("! IMU lost, easing servo to center");
        state = State::FAILSAFE;
        last_reinit_ms = now_ms;
        servo.easeToCenter();
      }
      break;
    }

    case State::FAILSAFE: {
      servo.easeToCenter();
      if (now_ms - last_blink_ms >= kFailBlinkMs) {
        last_blink_ms = now_ms;
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      }
      if (!imu_ok && now_ms - last_reinit_ms >= kReinitEveryMs) {
        last_reinit_ms = now_ms;
        imu.reinit();
      }
      if (imu_ok) {
        Serial.println("IMU back, resuming");
        reseed_filter = true;
        state = State::RUNNING;
        digitalWrite(PIN_LED, HIGH);
      }
      break;
    }
  }

  servo.update(dt);

  if (telemetry && now_ms - last_telemetry_ms >= kTelemetryMs) {
    last_telemetry_ms = now_ms;
    Serial.printf("raw %7.2f  filt %7.2f  servo %4u us\n",
                  last_accel_angle, filter.angle(), servo.currentUs());
  }
}
