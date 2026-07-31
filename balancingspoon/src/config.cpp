#include "config.h"

#include <Preferences.h>

static const char *kNamespace = "spoon";

void configLoad(Config &c) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return;  // namespace doesn't exist yet -> keep defaults
  }
  c.gain = prefs.getFloat("gain", c.gain);
  c.trim_deg = prefs.getFloat("trim", c.trim_deg);
  c.level_deg = prefs.getFloat("level", c.level_deg);
  c.invert = prefs.getBool("invert", c.invert);
  c.axis = prefs.getUChar("axis", c.axis);
  prefs.end();
}

void configSave(const Config &c) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("! NVS open failed, config not saved");
    return;
  }
  prefs.putFloat("gain", c.gain);
  prefs.putFloat("trim", c.trim_deg);
  prefs.putFloat("level", c.level_deg);
  prefs.putBool("invert", c.invert);
  prefs.putUChar("axis", c.axis);
  prefs.end();
}
