#include "app.h"

const char* const STATE_NAMES[ST_COUNT] = {"idle", "done", "busy", "waiting", "error"};
const char* const ACTION_NAMES[ACT_COUNT] = {"none", "dismiss", "brightness", "mute"};
const char* const ANIM_NAMES[AN_COUNT] = {"solid", "breathe", "blink", "spin", "comet", "wave", "heartbeat", "sparkle",
                                          "pendulum", "fill", "beacon"};

const size_t SETTINGS_V1_SIZE = 32;   // розмір блоку до появи анімацій

Settings    cfg;
uint8_t     brightness = 60;
Preferences prefs;

void loadSettings() {
  prefs.begin("agentlight");
  brightness = prefs.getUChar("bright", brightness);
  size_t stored = prefs.isKey("cfg") ? prefs.getBytesLength("cfg") : 0;
  if (stored == sizeof cfg) {
    prefs.getBytes("cfg", &cfg, sizeof cfg);
  } else if (stored == SETTINGS_V1_SIZE) {   // старий блок без анімацій: решту налаштувань зберігаємо
    prefs.getBytes("cfg", &cfg, SETTINGS_V1_SIZE);
    memcpy(cfg.anim, Settings().anim, sizeof cfg.anim);
  }
  for (auto& a : cfg.anim) if (a >= AN_COUNT) a = AN_SOLID;
}

void saveSettings() { prefs.putBytes("cfg", &cfg, sizeof cfg); }

void setBrightness(int value) {
  brightness = constrain(value, 5, (int)MAX_BRIGHTNESS);
  prefs.putUChar("bright", brightness);
}
