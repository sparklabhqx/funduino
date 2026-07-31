#pragma once
#include <Arduino.h>

// Accel in g, gyro in deg/s, already scaled from raw registers.
struct ImuSample {
  float ax, ay, az;
  float gx, gy, gz;
};

// Minimal MPU-6050 driver over raw Wire register access.
class Imu {
 public:
  // Starts I2C and configures the sensor. Returns false if any register
  // write is NACKed (sensor absent / bus fault).
  bool begin(uint8_t sda, uint8_t scl, uint8_t addr = 0x68);

  // Re-runs the register setup on the already-started bus (failsafe retry).
  bool reinit();

  // Burst-reads accel + temp + gyro (14 bytes) in one transaction.
  bool read(ImuSample &out);

 private:
  bool writeReg(uint8_t reg, uint8_t val);
  uint8_t _addr = 0x68;
};
