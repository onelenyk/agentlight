// Ігри на платі: логіка — у games.h, тут годинник, випадковість і збереження рекордів.
#include "app.h"

Games games;

static uint32_t hardwareRandom() { return esp_random(); }

void playBegin() {
  games.rnd = hardwareRandom;
  if (prefs.isKey("records") && prefs.getBytesLength("records") == sizeof games.best)
    prefs.getBytes("records", games.best, sizeof games.best);
  games.select(cfg.gameSel, millis());
}

void resetRecords() {
  memset(games.best, 0, sizeof games.best);
  memset(games.last, 0, sizeof games.last);
  prefs.remove("records");
}

void pollPlay() {
  if (cfg.mode != MODE_GAMES) return;
  games.leds = cfg.leds;
  games.dice = cfg.diceKind;
  games.tick(millis());
  if (games.recordChanged) {
    games.recordChanged = false;
    prefs.putBytes("records", games.best, sizeof games.best);
  }
}

void fillGame(JsonObject o) {
  static const char* const STAGES[Games::STAGE_COUNT] = {"ready", "wait", "go", "run", "count", "play", "echo", "spin", "result", "fail"};
  o["game"] = games.game;
  o["stage"] = STAGES[games.stage];
  o["dice"] = cfg.diceKind;
  o["tier"] = games.tier();
  JsonArray last = o["last"].to<JsonArray>(), best = o["best"].to<JsonArray>();
  for (uint8_t g = 0; g < Games::GAME_COUNT; g++) { last.add(games.last[g]); best.add(games.best[g]); }
}
