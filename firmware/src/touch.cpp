// Сенсорна кнопка TTP223 на TOUCH_PIN. Жести розпізнає gesture.h; тут — що кожен із них робить.
// Дотик, подвійний дотик і утримання виконують дії з налаштувань, свої в кожному режимі.
// Довге утримання (2 с) відкриває вибір режиму: лампа по черзі показує кольори режимів, відпустив — вибрав.
// У режимі ігор дотик іде прямо в гру в момент торкання, а утримання перемикає на наступну гру.
#include "app.h"
#include "gesture.h"

// Кольори, які перебирає дія «наступний колір» у режимі лампи
const uint32_t PALETTE[] = {0xFFB060, 0xFFFFFF, 0xFF2000, 0xFF5A00, 0xFFC800, 0x00FF1E, 0x00D0FF, 0x0040FF, 0x9000FF, 0xFF30A0};
const uint8_t  PALETTE_SIZE = sizeof PALETTE / sizeof PALETTE[0];

bool     touchRaw = false;
uint32_t touchCount = 0, touchAt = 0;
uint32_t signalCount = 0, signalAt = 0;
static Gestures gestures;

void touchBegin() { pinMode(TOUCH_PIN, INPUT_PULLDOWN); }   // без модуля пін не «плаває» і дотиків не вигадує

void runAction(uint8_t action, const char* gesture) {
  uint32_t workMs = cfg.focusWorkMin * 60000UL, breakMs = cfg.focusBreakMin * 60000UL;
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
      setMode((Mode)((cfg.mode + 1) % MODE_COUNT));
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
    case ACT_SLEEP:
      startSleep();
      break;
    case ACT_SIGNAL:       // агент побачить це в get_status
      signalCount++;
      signalAt = millis();
      flash(250);
      break;
    case ACT_WEBHOOK:
      fireWebhook(gesture);
      break;
    case ACT_SPARK:
      startRainbow(1500);
      break;
    case ACT_FOCUS_TOGGLE: focus.toggle(millis(), workMs); break;
    case ACT_FOCUS_SKIP:   focus.skip(millis(), workMs, breakMs); break;
    case ACT_FOCUS_RESET:  focus.reset(); break;
    case ACT_MEDIA_PLAY:   remoteMedia(0xCD); break;    // коди HID Consumer
    case ACT_MEDIA_NEXT:   remoteMedia(0xB5); break;
    case ACT_MEDIA_PREV:   remoteMedia(0xB6); break;
    case ACT_VOLUME_UP:    remoteMedia(0xE9); break;
    case ACT_VOLUME_DOWN:  remoteMedia(0xEA); break;
    case ACT_KEY_F13: case ACT_KEY_F14: case ACT_KEY_F15: case ACT_KEY_F16:
      remoteKey(0x68 + (action - ACT_KEY_F13));          // F13 у таблиці HID Keyboard — 0x68
      break;
  }
}

void pollTouch() {
  static int8_t pick = -1;                    // режим, який зараз показує вибір
  Mode mode = (Mode)cfg.mode;
  static bool inGame = false;                 // цей дотик уже передано грі: її ж треба сповістити про відпускання
  bool playing = mode == MODE_GAMES;
  gestures.doubleEnabled = touchAction(mode, G_DOUBLE) != ACT_NONE;   // немає подвійного — дотик не чекає другого
  touchRaw = digitalRead(TOUCH_PIN);          // TTP223: HIGH, поки палець на сенсорі
  Gestures::Event event = gestures.update(touchRaw, millis());
  if (inGame && !gestures.pressed) { inGame = false; games.release(millis()); }
  switch (event) {
    case Gestures::PRESS:
      touchCount++;
      touchAt = millis();
      if (playing) { inGame = true; games.press(millis()); }   // без спалаху й без очікування: у грі рахується мить
      else flash();
      break;
    case Gestures::HOLD_READY:
      if (playing) {                          // утримання — наступна гра; цей дотик грі вже не належить
        inGame = false;
        cfg.gameSel = (cfg.gameSel + 1) % Games::GAME_COUNT;
        saveSettings();
        games.select(cfg.gameSel, millis());
      } else flash();
      break;
    case Gestures::TAP:    runAction(touchAction(mode, G_TAP), "tap"); break;
    case Gestures::DOUBLE: runAction(touchAction(mode, G_DOUBLE), "double"); break;
    case Gestures::HOLD:   runAction(touchAction(mode, G_HOLD), "hold"); break;
    case Gestures::PICK_START:
      pick = (cfg.mode + 1) % MODE_COUNT;     // перший показаний — наступний за поточним
      lightPick(pick);
      break;
    case Gestures::PICK_STEP:
      pick = (pick + 1) % MODE_COUNT;
      lightPick(pick);
      break;
    case Gestures::PICK_END:
      lightPick(-1);
      setMode((Mode)pick);
      break;
    default: break;
  }
}
