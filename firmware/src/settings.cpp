#include "app.h"

const char* const STATE_NAMES[ST_COUNT] = {"idle", "done", "busy", "waiting", "error"};
const char* const MODE_NAMES[MODE_COUNT] = {"agents", "lamp"};
const char* const GESTURE_NAMES[G_COUNT] = {"tap", "double", "hold"};
const char* const ACTION_NAMES[ACT_COUNT] = {"none", "dismiss", "brightness", "toggle", "mode", "animation", "color"};
const char* const ANIM_NAMES[AN_COUNT] = {"solid", "breathe", "blink", "spin", "comet", "wave", "heartbeat", "sparkle",
                                          "pendulum", "fill", "beacon", "rainbow"};

Settings    cfg;
uint8_t     brightness = 60;
Preferences prefs;

bool actionFits(uint8_t action, Mode mode) {
  if (action == ACT_DISMISS) return mode == MODE_AGENTS;
  if (action == ACT_NEXT_ANIM || action == ACT_NEXT_COLOR) return mode == MODE_LAMP;
  return action < ACT_COUNT;
}

uint8_t& touchAction(Mode mode, Gesture gesture) {
  if (mode == MODE_LAMP) return cfg.lampTouch[gesture];
  return gesture == G_TAP ? cfg.tap : gesture == G_DOUBLE ? cfg.dbl : cfg.hold;
}

void setMode(Mode mode) {
  cfg.mode = mode;
  saveSettings();
  lightSwitch(true);
}

void loadSettings() {
  prefs.begin("agentlight");
  brightness = prefs.getUChar("bright", brightness);
  // Розміри блоку в старих версіях і поле, з якого в них починалось «ще не існує»
  const struct { size_t size, validUntil; } versions[] = {
    {32, offsetof(Settings, anim)},    // до анімацій
    {36, offsetof(Settings, mode)},    // до режимів
    {sizeof cfg, sizeof cfg},
  };
  size_t stored = prefs.isKey("cfg") ? prefs.getBytesLength("cfg") : 0;
  for (auto& v : versions) {
    if (stored != v.size) continue;
    Settings fresh;
    prefs.getBytes("cfg", &cfg, stored);
    memcpy((uint8_t*)&cfg + v.validUntil, (uint8_t*)&fresh + v.validUntil, sizeof cfg - v.validUntil);
    break;
  }
  for (auto& a : cfg.anim) if (a >= AN_COUNT) a = AN_SOLID;
  if (cfg.lampAnim >= AN_COUNT) cfg.lampAnim = AN_SOLID;
  if (cfg.mode >= MODE_COUNT) cfg.mode = MODE_AGENTS;
  for (uint8_t m = 0; m < MODE_COUNT; m++)
    for (uint8_t g = 0; g < G_COUNT; g++)
      if (!actionFits(touchAction((Mode)m, (Gesture)g), (Mode)m)) touchAction((Mode)m, (Gesture)g) = ACT_NONE;
}

void saveSettings() { prefs.putBytes("cfg", &cfg, sizeof cfg); }

void setBrightness(int value) {
  brightness = constrain(value, 5, (int)MAX_BRIGHTNESS);
  prefs.putUChar("bright", brightness);
}
