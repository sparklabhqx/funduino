#include "imu.h"

#include <Wire.h>

// MPU-6050 register map (subset)
static const uint8_t REG_SMPLRT_DIV = 0x19;
static const uint8_t REG_CONFIG = 0x1A;
static const uint8_t REG_GYRO_CONFIG = 0x1B;
static const uint8_t REG_ACCEL_CONFIG = 0x1C;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t REG_PWR_MGMT_1 = 0x6B;

// Scale factors for the ranges configured below.
static const float kAccelLsbPerG = 8192.0f;  // +/-4 g
static const float kGyroLsbPerDps = 65.5f;   // +/-500 deg/s

bool Imu::begin(uint8_t sda, uint8_t scl, uint8_t addr) {
  _addr = addr;
  Wire.begin(sda, scl, 400000);
  Wire.setTimeOut(5);  // ms; keeps a wedged bus from stalling the loop
  return reinit();
}

bool Imu::reinit() {
  bool ok = true;
  ok &= writeReg(REG_PWR_MGMT_1, 0x01);    // wake, PLL clock from X gyro
  ok &= writeReg(REG_SMPLRT_DIV, 0x04);    // 1 kHz / (1+4) = 200 Hz
  ok &= writeReg(REG_CONFIG, 0x03);        // DLPF 3: accel 44 Hz / gyro 42 Hz
  ok &= writeReg(REG_GYRO_CONFIG, 0x08);   // FS_SEL 1: +/-500 deg/s
  ok &= writeReg(REG_ACCEL_CONFIG, 0x08);  // AFS_SEL 1: +/-4 g
  return ok;
}

bool Imu::read(ImuSample &out) {
  Wire.beginTransmission(_addr);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) {  // repeated start
    return false;
  }
  if (Wire.requestFrom(_addr, (uint8_t)14) != 14) {
    return false;
  }
  uint8_t buf[14];
  for (uint8_t i = 0; i < 14; i++) {
    buf[i] = Wire.read();
  }
  auto be16 = [&buf](uint8_t i) {
    return (int16_t)((buf[i] << 8) | buf[i + 1]);
  };
  out.ax = be16(0) / kAccelLsbPerG;
  out.ay = be16(2) / kAccelLsbPerG;
  out.az = be16(4) / kAccelLsbPerG;
  // buf[6..7] = temperature, unused
  out.gx = be16(8) / kGyroLsbPerDps;
  out.gy = be16(10) / kGyroLsbPerDps;
  out.gz = be16(12) / kGyroLsbPerDps;
  return true;
}

bool Imu::writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
