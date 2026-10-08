// Сенсорна кнопка TTP223 на TOUCH_PIN: короткий дотик і утримання виконують дії з налаштувань.
#include "app.h"

const uint16_t DEBOUNCE_MS = 40;
const uint16_t HOLD_MS = 800;

bool     touchRaw = false;
uint32_t touchCount = 0, touchAt = 0;

void touchBegin() { pinMode(TOUCH_PIN, INPUT_PULLDOWN); }   // без модуля пін не «плаває» і дотиків не вигадує

static void runAction(uint8_t action) {
  switch (action) {
    case ACT_DISMISS:   // «побачив»: прибирає все, крім агентів, що працюють
      clearAgents(true);
      break;
    case ACT_BRIGHT:    // по колу: тьмяно, середньо, повна
      setBrightness(brightness < 40 ? 40 : brightness < 120 ? 120 : brightness < MAX_BRIGHTNESS ? MAX_BRIGHTNESS : 15);
      break;
    case ACT_MUTE:
      muted = !muted;
      break;
  }
}

void pollTouch() {
  static bool     pressed = false, held = false;
  static uint32_t changedAt = 0;
  touchRaw = digitalRead(TOUCH_PIN);    // TTP223: HIGH, поки палець на сенсорі
  if (touchRaw != pressed && millis() - changedAt > DEBOUNCE_MS) {
    pressed = touchRaw;
    if (pressed) {
      touchCount++;
      touchAt = millis();
      flash();
    } else if (!held) {
      runAction(cfg.tap);
    }
    changedAt = millis();
    held = false;
  }
  if (pressed && !held && millis() - changedAt > HOLD_MS) {
    held = true;
    flash();
    runAction(cfg.hold);
  }
}
