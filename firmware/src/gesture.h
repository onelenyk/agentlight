// Жести сенсора: з «палець на сенсорі / не на сенсорі» в часі робить події. Чистий C++ без Arduino —
// той самий код у прошивці (touch.cpp) і в тестах на комп'ютері (tests/logic_test.cpp).
//
//   PRESS       палець торкнувся (одразу; для підтвердження спалахом і для ігор)
//   TAP         короткий дотик. Якщо ввімкнено подвійний, приходить із затримкою DOUBLE_MS: чекаємо другого
//   DOUBLE      два короткі дотики поспіль
//   HOLD_READY  палець тримають уже HOLD_MS: відпустиш — буде HOLD
//   HOLD        відпустили після HOLD_MS, але до PICK_MS
//   PICK_START  тримають PICK_MS: почався вибір режиму
//   PICK_STEP   кожні PICK_STEP_MS далі: показати наступний режим
//   PICK_END    відпустили під час вибору: взяти показаний режим
#pragma once
#include <cstdint>

struct Gestures {
  enum Event { NONE, PRESS, TAP, DOUBLE, HOLD_READY, HOLD, PICK_START, PICK_STEP, PICK_END };
  static const uint32_t DEBOUNCE_MS = 40, HOLD_MS = 800, DOUBLE_MS = 350, PICK_MS = 2000, PICK_STEP_MS = 1100;

  bool doubleEnabled = true;       // false — TAP одразу після відпускання, без очікування другого дотику

  // Викликати часто (щомілісекунди-десятки); повертає щонайбільше одну подію за виклик
  Event update(bool raw, uint32_t now) {
    if (raw != pressed && now - changedAt > DEBOUNCE_MS) {
      pressed = raw;
      uint32_t held = now - changedAt;
      changedAt = now;
      if (pressed) {
        holdReady = picking = false;
        return PRESS;
      }
      if (picking) { picking = false; tapAt = 0; return PICK_END; }
      if (held >= HOLD_MS) { tapAt = 0; return HOLD; }
      if (!doubleEnabled) return TAP;
      if (tapAt) { tapAt = 0; return DOUBLE; }
      tapAt = now ? now : 1;       // 0 означає «дотик не чекає»
      return NONE;
    }
    if (pressed) {
      uint32_t held = now - changedAt;
      if (!holdReady && held >= HOLD_MS) { holdReady = true; return HOLD_READY; }
      if (!picking && held >= PICK_MS) { picking = true; stepAt = now; return PICK_START; }
      if (picking && now - stepAt >= PICK_STEP_MS) { stepAt = now; return PICK_STEP; }
    } else if (tapAt && now - tapAt > DOUBLE_MS) {
      tapAt = 0;
      return TAP;
    }
    return NONE;
  }

  bool pressed = false;

 private:
  bool     holdReady = false, picking = false;
  uint32_t changedAt = 0, tapAt = 0, stepAt = 0;
};
