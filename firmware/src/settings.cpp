#include "app.h"

const char* const STATE_NAMES[ST_COUNT] = {"idle", "done", "busy", "waiting", "error"};
const char* const ACTION_NAMES[ACT_COUNT] = {"none", "dismiss", "brightness", "mute"};

Settings    cfg;
uint8_t     brightness = 60;
Preferences prefs;

void loadSettings() {
  prefs.begin("agentlight");
  brightness = prefs.getUChar("bright", brightness);
  if (prefs.isKey("cfg") && prefs.getBytesLength("cfg") == sizeof cfg) prefs.getBytes("cfg", &cfg, sizeof cfg);
}

void saveSettings() { prefs.putBytes("cfg", &cfg, sizeof cfg); }

void setBrightness(int value) {
  brightness = constrain(value, 5, (int)MAX_BRIGHTNESS);
  prefs.putUChar("bright", brightness);
}
