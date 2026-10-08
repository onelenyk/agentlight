// Тести логіки прошивки, яка не залежить від плати: жести сенсора й таймер фокусу.
// Збирається й запускається з tests/test_agentlight.py; при помилці друкує рядок і виходить з кодом 1.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../firmware/src/focus.h"
#include "../firmware/src/gesture.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

typedef Gestures G;

// Програє сценарій «палець на сенсорі від-до» і повертає всі події по порядку
struct Touches { uint32_t from, to; };
static std::vector<G::Event> play(std::vector<Touches> touches, uint32_t until, bool doubleEnabled = true) {
  G g;
  g.doubleEnabled = doubleEnabled;
  std::vector<G::Event> events;
  for (uint32_t now = 100; now <= until; now += 5) {
    bool raw = false;
    for (auto& t : touches) raw |= now >= t.from && now < t.to;
    G::Event e = g.update(raw, now);
    if (e != G::NONE) events.push_back(e);
  }
  return events;
}
static bool same(const std::vector<G::Event>& got, std::vector<G::Event> want) { return got == want; }

static void gestures() {
  CHECK(same(play({{1000, 1150}}, 3000), {G::PRESS, G::TAP}));
  CHECK(same(play({{1000, 1150}}, 3000, false), {G::PRESS, G::TAP}));
  CHECK(same(play({{1000, 1150}, {1300, 1450}}, 3000), {G::PRESS, G::PRESS, G::DOUBLE}));
  // два дотики з великою паузою — це два окремі дотики
  CHECK(same(play({{1000, 1150}, {1700, 1850}}, 3000), {G::PRESS, G::TAP, G::PRESS, G::TAP}));
  // без подвійного дотику другий не чекає
  CHECK(same(play({{1000, 1150}, {1300, 1450}}, 3000, false), {G::PRESS, G::TAP, G::PRESS, G::TAP}));
  CHECK(same(play({{1000, 2200}}, 4000), {G::PRESS, G::HOLD_READY, G::HOLD}));
  // довге утримання: вибір режиму, і HOLD при цьому не спрацьовує
  CHECK(same(play({{1000, 3200}}, 5000), {G::PRESS, G::HOLD_READY, G::PICK_START, G::PICK_END}));
  CHECK(same(play({{1000, 5500}}, 7000), {G::PRESS, G::HOLD_READY, G::PICK_START, G::PICK_STEP, G::PICK_STEP, G::PICK_END}));
  // брязкіт контакту коротший за DEBOUNCE_MS не дає зайвих подій
  CHECK(same(play({{1000, 1010}, {1020, 1150}}, 3000), {G::PRESS, G::TAP}));
  // дотик одразу після утримання не зливається з ним у подвійний
  CHECK(same(play({{1000, 2200}, {2300, 2450}}, 4000), {G::PRESS, G::HOLD_READY, G::HOLD, G::PRESS, G::TAP}));

  // TAP без подвійного приходить у момент відпускання, з подвійним — на DOUBLE_MS пізніше
  G fast, slow;
  fast.doubleEnabled = false;
  uint32_t fastAt = 0, slowAt = 0;
  for (uint32_t now = 100; now <= 3000; now += 5) {
    bool raw = now >= 1000 && now < 1150;
    if (fast.update(raw, now) == G::TAP) fastAt = now;
    if (slow.update(raw, now) == G::TAP) slowAt = now;
  }
  CHECK(fastAt >= 1150 && fastAt <= 1160);
  CHECK(slowAt > 1150 + G::DOUBLE_MS && slowAt <= 1150 + G::DOUBLE_MS + 15);
}

static void focus() {
  const uint32_t WORK = 25 * 60000, BREAK = 5 * 60000;
  Focus f;
  CHECK(f.phase == Focus::IDLE && !f.running());
  CHECK(!f.tick(1000, WORK, BREAK));

  f.toggle(1000, WORK);
  CHECK(f.phase == Focus::WORK && f.running() && f.leftMs == WORK && f.totalMs == WORK);
  CHECK(!f.tick(1000 + 60000, WORK, BREAK));
  CHECK(f.leftMs == WORK - 60000);

  // пауза: час не йде, хоч скільки минуло
  f.toggle(61000, WORK);
  CHECK(f.paused && !f.running());
  CHECK(!f.tick(61000 + 3600000, WORK, BREAK));
  CHECK(f.leftMs == WORK - 60000);
  f.toggle(3661000, WORK);
  CHECK(f.running());
  CHECK(!f.tick(3661000 + 1000, WORK, BREAK));
  CHECK(f.leftMs == WORK - 61000);

  // кінець роботи: перерва, цикл зарахований, лампа сигналить
  uint32_t now = 3662000 + f.leftMs;
  CHECK(f.tick(now, WORK, BREAK));
  CHECK(f.phase == Focus::BREAK && f.leftMs == BREAK && f.cycles == 1);
  CHECK(f.alerting(now + 1000) && !f.alerting(now + Focus::ALERT_MS + 1));

  // кінець перерви: знову робота, циклів стільки ж
  now += BREAK;
  CHECK(f.tick(now, WORK, BREAK));
  CHECK(f.phase == Focus::WORK && f.leftMs == WORK && f.cycles == 1);

  // пропуск роботи цикл не зараховує
  f.skip(now + 5000, WORK, BREAK);
  CHECK(f.phase == Focus::BREAK && f.cycles == 1 && f.leftMs == BREAK);
  f.skip(now + 6000, WORK, BREAK);
  CHECK(f.phase == Focus::WORK);

  f.reset();
  CHECK(f.phase == Focus::IDLE && f.leftMs == 0 && f.cycles == 0 && !f.alerting(now + 7000));
  f.skip(now + 8000, WORK, BREAK);
  CHECK(f.phase == Focus::IDLE);

  // лічильник мілісекунд переповнюється раз на 49 діб: таймер має це пережити
  Focus w;
  w.toggle(0xFFFFFF00u, WORK);
  CHECK(!w.tick(0xFFFFFF00u + 1000, WORK, BREAK));
  CHECK(w.leftMs == WORK - 1000);
}

int main() {
  gestures();
  focus();
  if (failures) { printf("%d failed\n", failures); return 1; }
  printf("logic ok\n");
  return 0;
}
