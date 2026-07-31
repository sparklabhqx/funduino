#include "cli.h"

void Cli::begin(Config *cfg, bool *telemetry, ActionFn on_level,
                ActionFn on_axis_toggle, ActionFn on_save) {
  _cfg = cfg;
  _telemetry = telemetry;
  _on_level = on_level;
  _on_axis_toggle = on_axis_toggle;
  _on_save = on_save;
}

void Cli::poll() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (_len > 0) {
        _buf[_len] = '\0';
        handleLine(_buf);
        _len = 0;
      }
    } else if (_len < sizeof(_buf) - 1) {
      _buf[_len++] = c;
    }
  }
}

void Cli::printHelp() {
  Serial.println("Balance-spoon CLI (end commands with Enter):");
  Serial.println("  l        capture current pose as level");
  Serial.println("  i        toggle invert");
  Serial.println("  a        toggle axis (X roll / Y pitch)");
  Serial.println("  g <val>  set gain, e.g. g 1.15");
  Serial.println("  t <deg>  set trim, e.g. t -2.5");
  Serial.println("  s        save config to NVS");
  Serial.println("  d        toggle 20 Hz telemetry");
  Serial.println("  h or ?   this help");
}

void Cli::handleLine(char *line) {
  // skip leading spaces
  while (*line == ' ') line++;
  const char cmd = line[0];
  const char *arg = line + 1;
  while (*arg == ' ') arg++;

  switch (cmd) {
    case 'l':
      _on_level();
      break;
    case 'i':
      _cfg->invert = !_cfg->invert;
      Serial.printf("invert: %s\n", _cfg->invert ? "on" : "off");
      break;
    case 'a':
      _on_axis_toggle();
      Serial.printf("axis: %s\n", _cfg->axis == 0 ? "X (roll)" : "Y (pitch)");
      break;
    case 'g': {
      char *end = nullptr;
      float v = strtof(arg, &end);
      if (end == arg) {
        Serial.println("! usage: g <val>");
      } else {
        _cfg->gain = constrain(v, 0.0f, 5.0f);
        Serial.printf("gain: %.2f\n", _cfg->gain);
      }
      break;
    }
    case 't': {
      char *end = nullptr;
      float v = strtof(arg, &end);
      if (end == arg) {
        Serial.println("! usage: t <deg>");
      } else {
        _cfg->trim_deg = constrain(v, -30.0f, 30.0f);
        Serial.printf("trim: %.2f deg\n", _cfg->trim_deg);
      }
      break;
    }
    case 's':
      _on_save();
      break;
    case 'd':
      *_telemetry = !*_telemetry;
      Serial.printf("telemetry: %s\n", *_telemetry ? "on" : "off");
      break;
    case 'h':
    case '?':
      printHelp();
      break;
    default:
      Serial.printf("! unknown command '%c' (h for help)\n", cmd);
      break;
  }
}
