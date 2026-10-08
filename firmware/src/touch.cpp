// Сенсорна кнопка TTP223 на TOUCH_PIN. Три жести — дотик, подвійний дотик, утримання — виконують дії,
// призначені їм у налаштуваннях окремо для кожного режиму.
#include "app.h"

const uint16_t DEBOUNCE_MS = 40;
const uint16_t HOLD_MS = 800;
const uint16_t DOUBLE_MS = 350;     // другий дотик має початись не пізніше, ніж за стільки після першого

// Кольори, які перебирає дія «наступний колір» у режимі лампи
const uint32_t PALETTE[] = {0xFFB060, 0xFFFFFF, 0xFF2000, 0xFF5A00, 0xFFC800, 0x00FF1E, 0x00D0FF, 0x0040FF, 0x9000FF, 0xFF30A0};
const uint8_t  PALETTE_SIZE = sizeof PALETTE / sizeof PALETTE[0];

bool     touchRaw = false;
uint32_t touchCount = 0, touchAt = 0;

void touchBegin() { pinMode(TOUCH_PIN, INPUT_PULLDOWN); }   // без модуля пін не «плаває» і дотиків не вигадує

static void runAction(uint8_t action) {
  switch (action) {
    case ACT_DISMISS:      // «побачив»: прибирає все, крім агентів, що працюють
      clearAgents(true);
      break;
    case ACT_BRIGHT:       // по колу: тьмяно, середньо, повна
      setBrightness(brightness < 40 ? 40 : brightness < 120 ? 120 : brightness < MAX_BRIGHTNESS ? MAX_BRIGHTNESS : 15);
      break;
    case ACT_TOGGLE:
      lightSwitch(lightOff);
      break;
    case ACT_MODE:
      setMode(cfg.mode == MODE_AGENTS ? MODE_LAMP : MODE_AGENTS);
      break;
    case ACT_NEXT_ANIM:
      cfg.lampAnim = (cfg.lampAnim + 1) % AN_COUNT;
      saveSettings();
      lightSwitch(true);
      break;
    case ACT_NEXT_COLOR: {
      uint8_t i = 0;
      while (i < PALETTE_SIZE && PALETTE[i] != cfg.lampColor) i++;
      cfg.lampColor = PALETTE[(i + 1) % PALETTE_SIZE];   // власний колір поза палітрою переходить на перший
      saveSettings();
      lightSwitch(true);
      break;
    }
  }
}

static void run(Gesture gesture) { runAction(touchAction((Mode)cfg.mode, gesture)); }

void pollTouch() {
  static bool     pressed = false, held = false;
  static uint32_t changedAt = 0, tapAt = 0;   // tapAt: перший дотик чекає, чи не буде другого
  touchRaw = digitalRead(TOUCH_PIN);          // TTP223: HIGH, поки палець на сенсорі
  if (touchRaw != pressed && millis() - changedAt > DEBOUNCE_MS) {
    pressed = touchRaw;
    if (pressed) {
      touchCount++;
      touchAt = millis();
      flash();
    } else if (!held) {
      if (touchAction((Mode)cfg.mode, G_DOUBLE) == ACT_NONE) run(G_TAP);   // подвійного немає — не чекаємо
      else if (tapAt) { tapAt = 0; run(G_DOUBLE); }
      else tapAt = millis();
    }
    changedAt = millis();
    held = false;
  }
  if (tapAt && !pressed && millis() - tapAt > DOUBLE_MS) {
    tapAt = 0;
    run(G_TAP);
  }
  if (pressed && !held && millis() - changedAt > HOLD_MS) {
    held = true;
    tapAt = 0;
    flash();
    run(G_HOLD);
  }
}
