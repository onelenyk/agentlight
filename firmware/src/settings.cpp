#include "app.h"

const char* const STATE_NAMES[ST_COUNT] = {"idle", "done", "busy", "waiting", "error"};
const char* const MODE_NAMES[MODE_COUNT] = {"agents", "lamp", "focus", "games", "music"};
const uint32_t    MODE_COLORS[MODE_COUNT] = {0xFF5A00, 0xFFB060, 0xFF2D55, 0x0060FF, 0x9000FF};   // такими лампа показує режими під час вибору
const char* const GESTURE_NAMES[G_COUNT] = {"tap", "double", "hold"};
const char* const ACTION_NAMES[ACT_COUNT] = {"none", "dismiss", "brightness", "toggle", "mode", "animation", "color",
                                             "sleep", "signal", "webhook", "spark", "focus_toggle", "focus_skip", "focus_reset",
                                             "media_play", "media_next", "media_prev", "volume_up", "volume_down",
                                             "key_f13", "key_f14", "key_f15", "key_f16"};
const char* const ANIM_NAMES[AN_COUNT] = {"solid", "breathe", "blink", "spin", "comet", "wave", "heartbeat", "sparkle",
                                          "pendulum", "fill", "beacon", "rainbow"};

Settings    cfg;
uint8_t     brightness = 60;
Preferences prefs;
String      webhookUrl;

bool actionFits(uint8_t action, Mode mode) {
  if (action == ACT_DISMISS) return mode == MODE_AGENTS;
  if (action == ACT_NEXT_ANIM || action == ACT_NEXT_COLOR || action == ACT_SLEEP) return mode == MODE_LAMP;
  if (action == ACT_FOCUS_TOGGLE || action == ACT_FOCUS_SKIP || action == ACT_FOCUS_RESET) return mode == MODE_FOCUS;
  return action < ACT_COUNT;
}

uint8_t& touchAction(Mode mode, Gesture gesture) {
  if (mode == MODE_LAMP) return cfg.lampTouch[gesture];
  if (mode == MODE_FOCUS) return cfg.focusTouch[gesture];
  if (mode == MODE_MUSIC) return cfg.musicTouch[gesture];
  if (mode == MODE_GAMES) { static uint8_t fixed; fixed = ACT_NONE; return fixed; }   // в іграх жести зайняті самою грою
  return gesture == G_TAP ? cfg.tap : gesture == G_DOUBLE ? cfg.dbl : cfg.hold;
}

void setWebhook(const String& url) {
  webhookUrl = url;
  webhookUrl.trim();
  if (webhookUrl.length()) prefs.putString("webhook", webhookUrl); else prefs.remove("webhook");
}

void setMode(Mode mode) {
  cfg.mode = mode;
  saveSettings();
  lightSwitch(true);
  if (mode == MODE_GAMES) games.select(cfg.gameSel, millis());
}

void loadSettings() {
  prefs.begin("agentlight");
  brightness = prefs.getUChar("bright", brightness);
  if (prefs.isKey("webhook")) webhookUrl = prefs.getString("webhook");
  // Розміри блоку в старих версіях і поле, з якого в них починалось «ще не існує»
  const struct { size_t size, validUntil; } versions[] = {
    {32, offsetof(Settings, anim)},    // до анімацій
    {36, offsetof(Settings, mode)},    // до режимів
    {48, offsetof(Settings, focusWorkMin)},   // до таймера фокусу
    {60, offsetof(Settings, musicTouch)},     // до пульта
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
  if (cfg.gameSel >= Games::GAME_COUNT) cfg.gameSel = Games::REACTION;
  if (cfg.diceKind >= Games::DICE_COUNT) cfg.diceKind = Games::YES_NO;
  for (uint8_t m = 0; m < MODE_COUNT; m++)
    for (uint8_t g = 0; g < G_COUNT; g++)
      if (!actionFits(touchAction((Mode)m, (Gesture)g), (Mode)m)) touchAction((Mode)m, (Gesture)g) = ACT_NONE;
}

void saveSettings() { prefs.putBytes("cfg", &cfg, sizeof cfg); }

void setBrightness(int value) {
  brightness = constrain(value, 5, (int)MAX_BRIGHTNESS);
  prefs.putUChar("bright", brightness);
}
