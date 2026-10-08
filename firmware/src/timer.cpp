// Таймер фокусу на платі: логіка — у focus.h, тут лише годинник і налаштування. Іде в будь-якому режимі лампи.
#include "app.h"

Focus focus;

void pollFocus() {
  focus.tick(millis(), cfg.focusWorkMin * 60000UL, cfg.focusBreakMin * 60000UL);
}

void fillFocus(JsonObject o) {
  static const char* const PHASES[] = {"idle", "work", "break"};
  o["phase"] = PHASES[focus.phase];
  o["paused"] = focus.paused;
  o["left"] = (focus.leftMs + 999) / 1000;
  o["total"] = focus.totalMs / 1000;
  o["cycles"] = focus.cycles;
}
