#pragma once
#include <Arduino.h>

#include "config.h"

// Line-based serial CLI (send with Enter):
//   l         capture current pose as level
//   i         toggle invert
//   a         toggle axis (X roll / Y pitch)
//   g <val>   set gain
//   t <deg>   set trim
//   s         save config to NVS
//   d         toggle 20 Hz telemetry
//   h / ?     help
class Cli {
 public:
  using ActionFn = void (*)();

  void begin(Config *cfg, bool *telemetry, ActionFn on_level,
             ActionFn on_axis_toggle, ActionFn on_save);
  void poll();
  void printHelp();

 private:
  void handleLine(char *line);

  Config *_cfg = nullptr;
  bool *_telemetry = nullptr;
  ActionFn _on_level = nullptr;
  ActionFn _on_axis_toggle = nullptr;
  ActionFn _on_save = nullptr;
  char _buf[32];
  uint8_t _len = 0;
};
