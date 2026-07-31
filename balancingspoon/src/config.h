#pragma once
#include <Arduino.h>

// Runtime-tunable configuration, persisted to NVS on demand ('s' in the CLI).
struct Config {
  float gain = 1.0f;       // servo deg per measured deg of base tilt
  float trim_deg = 0.0f;   // manual level trim, added to the error
  float level_deg = 0.0f;  // captured "this pose is level" offset
  bool invert = false;     // flips servo direction
  uint8_t axis = 0;        // 0 = X (roll), 1 = Y (pitch)
};

void configLoad(Config &c);
void configSave(const Config &c);
