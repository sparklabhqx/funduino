# Balancing Spoon

One-axis self-balancing spoon: an MPU-6050 measures base tilt, a micro servo
counter-rotates the spoon clamp so the spoon stays level while the base tilts
around the servo shaft axis.

Built with PlatformIO (`esp32dev`, Arduino framework). `pio run -t upload` and
you're in business.

## Hardware

| Signal       | Pin                             |
|--------------|---------------------------------|
| I2C SDA      | GPIO25                          |
| I2C SCL      | GPIO26                          |
| MPU-6050     | addr 0x68 (AD0 low), 3.3 V      |
| Servo signal | GPIO27 (5 V supply, common GND) |
| Status LED   | GPIO2 (onboard)                 |

Power the servo from 5 V (VIN/USB), not from the 3.3 V regulator, and keep a
common ground between ESP32, MPU-6050 and servo. A 470 µF capacitor across
the servo supply helps against twitching from current spikes.

## How it works

- **Sense (200 Hz):** raw-register MPU-6050 driver over I2C — ±4 g, ±500 °/s,
  ~44 Hz DLPF, one 14-byte burst read per tick.
- **Fuse:** complementary filter (alpha 0.98) — the integrated gyro provides
  the fast, smooth part of the angle; the accelerometer slowly pulls out the
  drift.
- **Control:** `servo = sign · gain · (angle − level + trim)`, so the servo
  counter-rotates the base tilt and the spoon stays level.
- **Output stage:** One Euro adaptive smoothing (calm at rest, responsive in
  motion), 300 °/s slew limit, ±60° software travel, 50 Hz servo PWM at
  500–2500 µs with a 16-bit LEDC timer (the library default of 10 bits
  quantizes to ~1.8° steps and makes slow motion visibly stutter).

## Behavior

- **Boot:** 2 s gyro bias calibration — keep the device still. The LED blinks
  during calibration, then goes solid.
- **Failsafe:** if the IMU stops answering for >100 ms, the servo eases to
  center and the LED blinks fast; I2C setup is retried every 500 ms and
  control resumes automatically. The loop is fully non-blocking (no
  `delay()` anywhere).

## Serial CLI (115200 baud, end commands with Enter)

```
l        capture current pose as level
i        toggle invert (servo direction)
a        toggle axis (X roll / Y pitch)
g <val>  set gain, e.g. g 1.15
t <deg>  set trim, e.g. t -2.5
s        save config to NVS (survives reboot)
d        toggle 20 Hz telemetry (raw angle, filtered angle, servo µs)
h or ?   help
```

## First-run setup

1. `pio run -t upload && pio device monitor`
2. Hold the base level, wait for calibration, then send `l` to capture level.
3. Tilt the base: if the spoon rotates the wrong way, send `i`.
4. If your servo shaft lines up with the other IMU axis, send `a`.
5. Fine-tune with `g`/`t`, then `s` to persist.
