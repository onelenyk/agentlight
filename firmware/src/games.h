// Ігри на одному сенсорі й кількох діодах. Чистий C++ без Arduino — у прошивці (play.cpp) і в тестах на комп'ютері.
// Грі кажуть, коли палець торкнувся й коли відпустив, і часто викликають tick(); вона каже, чим світити кожному діоду.
//
//   REACTION  червоне — чекай; зелене — торкнись якнайшвидше. Результат: мілісекунди, менше — краще
//   STOP      вогник біжить по колу; зупини його на позначеному діоді. Результат: влучань поспіль, більше — краще
//   TEN       торкнись, і ще раз — коли, на твою думку, минуло 10 секунд. Результат: помилка в мс, менше — краще
//   RHYTHM    лампа блимає короткі й довгі спалахи; повтори їх дотиками. Результат: пройдено раундів
//   MARATHON  скільки дотиків за 10 секунд
//   DICE      вирішувач: так чи ні, число, колір. Рекорду немає
#pragma once
#include <cstdint>

struct Games {
  enum Game : uint8_t { REACTION, STOP, TEN, RHYTHM, MARATHON, DICE, GAME_COUNT };
  // ECHO — гравець повторює ритм (назву INPUT зайняв макрос Arduino)
  enum Stage : uint8_t { READY, WAIT, GO, RUN, COUNT, PLAY, ECHO, SPIN, RESULT, FAIL, STAGE_COUNT };
  enum Dice : uint8_t { YES_NO, NUMBER, COLOR, DICE_COUNT };
  static const uint8_t  MAX_RHYTHM = 12;
  static const uint32_t RESULT_MS = 3500, FAIL_MS = 1500, GO_TIMEOUT_MS = 3000, TEN_MS = 10000, MARATHON_MS = 10000,
                        SPIN_MS = 1200, LONG_MS = 350, ECHO_TIMEOUT_MS = 5000, FLASH_MS = 400;

  uint8_t  game = REACTION, stage = READY, dice = YES_NO, leds = 3;
  uint32_t last[GAME_COUNT] = {}, best[GAME_COUNT] = {};   // 0 — ще немає
  bool     recordChanged = false;                          // хтось побив рекорд: час зберегти best[]
  uint32_t (*rnd)() = nullptr;                             // джерело випадковості; у тестах — передбачуване

  static bool lowerIsBetter(uint8_t g) { return g == REACTION || g == TEN; }
  bool idle() const { return stage == READY; }

  void select(uint8_t g, uint32_t now) {
    game = g % GAME_COUNT;
    enter(READY, now);
    pressed = false;
    flashUntil = 0;
  }

  void press(uint32_t now) {
    pressed = true;
    pressAt = now;
    bool fresh = stage == READY || stage == RESULT || stage == FAIL;   // можна починати новий раунд
    switch (game) {
      case REACTION:
        if (fresh) { waitMs = 2000 + rnd() % 4001; enter(WAIT, now); }
        else if (stage == WAIT) enter(FAIL, now);                      // фальстарт
        else if (stage == GO) finish(now - stageAt, now);
        break;
      case STOP:
        if (fresh) { level = 0; startPos = rnd() % leds; target = rnd() % leds; enter(RUN, now); }
        else if (stage == RUN) {
          if (runner(now) != target) { finish(level, now); break; }
          level++;
          startPos = (target + 1) % leds;                              // далі біжить із наступного діода, уже швидше
          target = rnd() % leds;
          stageAt = now;
          flashUntil = now + FLASH_MS;
        }
        break;
      case TEN:
        if (fresh) enter(COUNT, now);
        else if (stage == COUNT && now - stageAt >= 1000)              // випадковий другий дотик одразу після першого — не рахуємо
          finish(now - stageAt > TEN_MS ? now - stageAt - TEN_MS : TEN_MS - (now - stageAt), now);
        break;
      case RHYTHM:
        if (fresh) { rounds = 0; length = 3; newPattern(now, 0); }
        break;                                                         // під час ECHO спалах міряється на відпусканні
      case MARATHON:
        if (fresh) { count = 1; enter(COUNT, now); }
        else if (stage == COUNT) count++;
        break;
      case DICE:
        if (stage != SPIN) {
          uint32_t r = rnd();
          value = dice == YES_NO ? r % 2 : dice == NUMBER ? 1 + r % leds : r % 360;
          enter(SPIN, now);
        }
        break;
    }
  }

  void release(uint32_t now) {
    pressed = false;
    if (game != RHYTHM || stage != ECHO) return;
    bool isLong = now - pressAt >= LONG_MS;
    inputAt = now;
    if (isLong != pattern[entered]) { finish(rounds, now); return; }
    if (++entered < length) return;
    rounds++;
    if (length < MAX_RHYTHM) length++;
    flashUntil = now + FLASH_MS;
    newPattern(now, 2 * FLASH_MS);
  }

  void tick(uint32_t now) {
    uint32_t in = now - stageAt;
    switch (stage) {
      case WAIT:   if (in >= waitMs) enter(GO, now); break;
      case GO:     if (in >= GO_TIMEOUT_MS) enter(FAIL, now); break;
      case RUN:    if (in >= 60000) enter(READY, now); break;          // покинули гру
      case COUNT:
        if (game == MARATHON && in >= MARATHON_MS) finish(count, now);
        else if (game == TEN && in >= 3 * TEN_MS) enter(FAIL, now);
        break;
      case PLAY:   if (now >= playEnd) { entered = 0; inputAt = now; enter(ECHO, now); } break;
      case ECHO:  if (!pressed && now - inputAt >= ECHO_TIMEOUT_MS) finish(rounds, now); break;
      case SPIN:   if (in >= SPIN_MS) { last[DICE] = value; enter(RESULT, now); } break;
      case RESULT: if (game != DICE && in >= RESULT_MS) enter(READY, now); break;   // кубик показує відповідь до наступного дотику
      case FAIL:   if (in >= FAIL_MS) enter(READY, now); break;
      default: break;
    }
  }

  // Колір кожного діода, 0xRRGGBB
  void render(uint32_t now, uint32_t* out) const {
    static const uint32_t IDENTITY[GAME_COUNT] = {0x00A0FF, 0x00FFC0, 0xFFFFFF, 0x8000FF, 0xFFC000, 0xFF00A0};
    uint32_t in = now - stageAt, all = 0;
    bool blink = (now / 120) % 2;
    for (uint8_t i = 0; i < leds; i++) out[i] = 0;
    if (flashUntil && now < flashUntil) all = 0x00FF1E;                              // влучив / раунд пройдено
    else switch (stage) {
      case READY:  all = dim(IDENTITY[game], in < 600 ? 1.0f : 0.12f + 0.1f * wave(now, 2000)); break;   // щойно вибрана гра — яскраво
      case WAIT:   all = 0xFF0000; break;
      case GO:     all = 0x00FF1E; break;
      case FAIL:   all = blink ? 0xFF0000 : 0x200000; break;
      case RESULT:
        if (game == DICE) {
          if (dice == NUMBER) { for (uint8_t i = 0; i < leds && i < value; i++) out[i] = 0xFFFFFF; return; }
          all = dice == YES_NO ? (value ? 0x00FF1E : 0xFF0000) : hue(value);
        } else all = tierColor(tier());
        break;
      case RUN: {
        uint8_t at = runner(now);
        for (uint8_t i = 0; i < leds; i++) out[i] = i == at ? (i == target ? 0x00FF40 : 0x00C0FF) : i == target ? 0x383838 : 0;
        return;
      }
      case COUNT:
        if (game == TEN) all = in < 1500 ? dim(0xFFFFFF, 1.0f - in / 1500.0f) : 0;   // показали «пішло» і далі темно
        else {                                                                       // марафон: шкала часу, спалах на кожен дотик
          float lit = (float)in / MARATHON_MS * leds;
          for (uint8_t i = 0; i < leds; i++) out[i] = pressed ? 0xFFFFFF : dim(0xFFC000, lit >= i + 1 ? 1.0f : lit > i ? lit - i : 0.04f);
          return;
        }
        break;
      case PLAY:   all = flashOn(now) ? 0x2060FF : 0; break;
      case ECHO:  all = pressed ? 0xFFFFFF : 0x040410; break;
      case SPIN: {                                                                   // крутиться й сповільнюється
        uint32_t step = in < SPIN_MS / 2 ? 60 : 60 + (in - SPIN_MS / 2) / 4;
        out[(in / step) % leds] = hue((in / step) * 67 % 360);
        return;
      }
      default: break;
    }
    for (uint8_t i = 0; i < leds; i++) out[i] = all;
  }

  // Наскільки добрий останній результат: 0 — чудово, 4 — слабко
  uint8_t tier() const {
    static const uint32_t LIMITS[GAME_COUNT][4] = {
      {250, 350, 500, 700}, {10, 6, 3, 1}, {200, 500, 1000, 2000}, {6, 4, 2, 1}, {70, 55, 40, 25}, {0, 0, 0, 0}};
    uint32_t v = last[game];
    for (uint8_t t = 0; t < 4; t++)
      if (lowerIsBetter(game) ? v <= LIMITS[game][t] : v >= LIMITS[game][t]) return t;
    return 4;
  }

  uint8_t runner(uint32_t now) const {                // на якому діоді зараз вогник
    uint32_t step = level >= 9 ? 75 : 300 - 25 * level;
    return (startPos + (now - stageAt) / step) % leds;
  }
  uint8_t  rhythmLength() const { return length; }
  bool     rhythmLong(uint8_t i) const { return pattern[i]; }

 private:
  bool     pressed = false, pattern[MAX_RHYTHM] = {};
  uint8_t  level = 0, startPos = 0, target = 0, length = 3, entered = 0;
  uint32_t stageAt = 0, pressAt = 0, waitMs = 0, count = 0, rounds = 0, value = 0, flashUntil = 0,
           playStart = 0, playEnd = 0, inputAt = 0;

  void enter(uint8_t next, uint32_t now) { stage = next; stageAt = now; }

  void finish(uint32_t result, uint32_t now) {
    last[game] = result;
    if (!lowerIsBetter(game) && result == 0) { enter(FAIL, now); return; }           // нуль влучань — не результат
    if (!best[game] || (lowerIsBetter(game) ? result < best[game] : result > best[game])) {
      best[game] = result;
      recordChanged = true;
    }
    enter(RESULT, now);
  }

  // Спалах — 200 мс короткий або 600 мс довгий, між ними 300 мс темряви
  void newPattern(uint32_t now, uint32_t delay) {
    playStart = now + delay + 400;
    uint32_t at = playStart;
    for (uint8_t i = 0; i < length; i++) { pattern[i] = rnd() % 2; at += (pattern[i] ? 600 : 200) + 300; }
    playEnd = at;
    enter(PLAY, now);
  }

  bool flashOn(uint32_t now) const {
    uint32_t at = playStart;
    for (uint8_t i = 0; i < length; i++) {
      uint32_t on = pattern[i] ? 600 : 200;
      if (now >= at && now < at + on) return true;
      at += on + 300;
    }
    return false;
  }

  static uint32_t dim(uint32_t rgb, float k) {
    return (uint32_t)((rgb >> 16 & 255) * k) << 16 | (uint32_t)((rgb >> 8 & 255) * k) << 8 | (uint32_t)((rgb & 255) * k);
  }
  static float wave(uint32_t now, uint32_t period) {    // 0 → 1 → 0, трикутником
    float x = (float)(now % period) / period;
    return x < 0.5f ? 2 * x : 2 - 2 * x;
  }
  static uint32_t tierColor(uint8_t t) {
    static const uint32_t COLORS[5] = {0x00FF1E, 0x80FF00, 0xFFD000, 0xFF6000, 0xFF0000};
    return COLORS[t];
  }
  static uint32_t hue(uint32_t degrees) {               // насичений колір за кутом на колірному колі
    uint32_t h = degrees % 360, x = (h % 60) * 255 / 60;
    switch (h / 60) {
      case 0: return 0xFF0000 | x << 8;
      case 1: return (255 - x) << 16 | 0x00FF00;
      case 2: return 0x00FF00 | x;
      case 3: return (255 - x) << 8 | 0x0000FF;
      case 4: return x << 16 | 0x0000FF;
      default: return 0xFF0000 | (255 - x);
    }
  }
};
