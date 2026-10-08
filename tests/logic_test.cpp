// Тести логіки прошивки, яка не залежить від плати: жести сенсора, таймер фокусу, ігри.
// Збирається й запускається з tests/test_agentlight.py; при помилці друкує рядок і виходить з кодом 1.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../firmware/src/focus.h"
#include "../firmware/src/games.h"
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

// Передбачувана «випадковість»: тест сам задає, що випаде далі
static std::vector<uint32_t> queue;
static size_t taken = 0;
static uint32_t scripted() { return taken < queue.size() ? queue[taken++] : 0; }
static void script(std::vector<uint32_t> values) { queue = values; taken = 0; }
static Games fresh(uint8_t game) {
  Games g;
  g.rnd = scripted;
  g.select(game, 1000);
  return g;
}
static void tap(Games& g, uint32_t at, uint32_t held = 100) { g.press(at); g.tick(at); g.release(at + held); g.tick(at + held); }

static void games() {
  typedef Games Gm;
  uint32_t leds[3];

  // --- Реакція
  script({1000});                                   // пауза 2000 + 1000 мс
  Gm r = fresh(Gm::REACTION);
  CHECK(r.idle());
  tap(r, 2000);
  CHECK(r.stage == Gm::WAIT);
  r.render(2500, leds); CHECK(leds[0] == 0xFF0000);
  r.tick(4999); CHECK(r.stage == Gm::WAIT);
  r.tick(5000); CHECK(r.stage == Gm::GO);
  r.render(5001, leds); CHECK(leds[2] == 0x00FF1E);
  r.press(5230);
  CHECK(r.stage == Gm::RESULT && r.last[Gm::REACTION] == 230 && r.best[Gm::REACTION] == 230 && r.recordChanged);
  CHECK(r.tier() == 0);
  r.tick(5230 + Gm::RESULT_MS); CHECK(r.idle());
  // гірший результат рекорду не міняє
  r.recordChanged = false;
  script({0});
  tap(r, 10000); r.tick(12000); r.press(12600);
  CHECK(r.last[Gm::REACTION] == 600 && r.best[Gm::REACTION] == 230 && !r.recordChanged && r.tier() == 3);
  // фальстарт
  script({3000});
  r.tick(20000); tap(r, 20000); r.press(21000);
  CHECK(r.stage == Gm::FAIL && r.best[Gm::REACTION] == 230);
  r.tick(21000 + Gm::FAIL_MS); CHECK(r.idle());
  // не торкнувся на зелене
  script({0});
  tap(r, 30000); r.tick(32000); CHECK(r.stage == Gm::GO);
  r.tick(32000 + Gm::GO_TIMEOUT_MS); CHECK(r.stage == Gm::FAIL);

  // --- Стоп-вогник: вогник стартує з діода 0, ціль — діод 2, крок 300 мс
  script({0, 2, 1});
  Gm s = fresh(Gm::STOP);
  s.press(2000);
  CHECK(s.stage == Gm::RUN && s.runner(2000) == 0 && s.runner(2300) == 1 && s.runner(2650) == 2 && s.runner(2900) == 0);
  s.press(2650);                                     // влучив; нова ціль — діод 1, вогник далі з діода 0 і швидше
  CHECK(s.stage == Gm::RUN && s.runner(2650) == 0 && s.runner(2650 + 275) == 1);
  s.press(2650 + 10);                                // мимо
  CHECK(s.stage == Gm::RESULT && s.last[Gm::STOP] == 1 && s.best[Gm::STOP] == 1);
  // перший же дотик мимо — це не результат
  script({0, 2});
  Gm s2 = fresh(Gm::STOP);
  s2.press(2000); s2.press(2010);
  CHECK(s2.stage == Gm::FAIL && s2.best[Gm::STOP] == 0);

  // --- Десять секунд
  Gm t = fresh(Gm::TEN);
  t.press(2000); t.release(2100);
  t.press(2400); CHECK(t.stage == Gm::COUNT);        // випадковий другий дотик у першу секунду ігнорується
  t.render(5000, leds); CHECK(leds[0] == 0);          // під час відліку темно
  t.press(12300);
  CHECK(t.stage == Gm::RESULT && t.last[Gm::TEN] == 300 && t.tier() == 1);
  Gm t2 = fresh(Gm::TEN);
  t2.press(2000); t2.press(11500);
  CHECK(t2.last[Gm::TEN] == 500);                    // раніше чи пізніше — рахується модуль помилки

  // --- Ритм: короткий, довгий, короткий
  script({0, 1, 0, 1, 1, 0, 0});
  Gm h = fresh(Gm::RHYTHM);
  h.press(2000); h.release(2100);
  CHECK(h.stage == Gm::PLAY && h.rhythmLength() == 3 && !h.rhythmLong(0) && h.rhythmLong(1) && !h.rhythmLong(2));
  h.render(2450, leds); CHECK(leds[0] == 0x2060FF);   // іде перший спалах
  h.render(2650, leds); CHECK(leds[0] == 0);           // пауза між спалахами
  for (uint32_t now = 2100; now < 6000 && h.stage == Gm::PLAY; now += 10) h.tick(now);
  CHECK(h.stage == Gm::ECHO);
  tap(h, 6000, 100); tap(h, 6500, 500); tap(h, 7300, 100);
  CHECK(h.stage == Gm::PLAY && h.rhythmLength() == 4);  // раунд пройдено, наступний довший
  for (uint32_t now = 7400; now < 14000 && h.stage == Gm::PLAY; now += 10) h.tick(now);
  CHECK(h.stage == Gm::ECHO);
  tap(h, 14000, 100);                                  // а треба було довгий
  CHECK(h.stage == Gm::RESULT && h.last[Gm::RHYTHM] == 1 && h.best[Gm::RHYTHM] == 1);
  // мовчання під час повторення — кінець гри
  script({0, 0, 0});
  Gm h2 = fresh(Gm::RHYTHM);
  h2.press(2000); h2.release(2100);
  for (uint32_t now = 2100; now < 6000 && h2.stage == Gm::PLAY; now += 10) h2.tick(now);
  for (uint32_t now = 6000; now < 6000 + Gm::ECHO_TIMEOUT_MS + 2000 && h2.stage == Gm::ECHO; now += 10) h2.tick(now);
  CHECK(h2.stage == Gm::FAIL);

  // --- Марафон
  Gm m = fresh(Gm::MARATHON);
  for (int i = 0; i < 60; i++) tap(m, 2000 + i * 150, 50);
  CHECK(m.stage == Gm::COUNT);
  m.tick(2000 + Gm::MARATHON_MS);
  CHECK(m.stage == Gm::RESULT && m.last[Gm::MARATHON] == 60 && m.tier() == 1);
  m.press(2000 + Gm::MARATHON_MS + 100);              // дотик після кінця починає новий забіг, а не дописується
  CHECK(m.stage == Gm::COUNT && m.last[Gm::MARATHON] == 60);

  // --- Кубик
  script({7, 5, 200});
  Gm d = fresh(Gm::DICE);
  d.dice = Gm::YES_NO;
  d.press(2000); CHECK(d.stage == Gm::SPIN);
  d.press(2100); CHECK(d.stage == Gm::SPIN);          // поки крутиться, дотик нічого не міняє
  d.tick(2000 + Gm::SPIN_MS);
  CHECK(d.stage == Gm::RESULT && d.last[Gm::DICE] == 1);
  d.render(9000, leds); CHECK(leds[0] == 0x00FF1E);
  d.tick(2000 + Gm::SPIN_MS + 60000); CHECK(d.stage == Gm::RESULT);   // відповідь лишається, доки не торкнешся
  d.dice = Gm::NUMBER;
  d.press(70000); d.tick(70000 + Gm::SPIN_MS);
  CHECK(d.last[Gm::DICE] == 3);                        // 1 + 5 % 3
  d.render(80000, leds); CHECK(leds[0] == 0xFFFFFF && leds[2] == 0xFFFFFF);
  CHECK(d.best[Gm::DICE] == 0);

  // --- Зміна гри посеред раунду повертає в початок
  script({0});
  Gm c = fresh(Gm::REACTION);
  tap(c, 2000); CHECK(c.stage == Gm::WAIT);
  c.select(Gm::TEN, 2500);
  CHECK(c.idle() && c.game == Gm::TEN);
  c.select(Gm::GAME_COUNT + 1, 2600); CHECK(c.game == 1);   // номер поза списком не ламає
}

int main() {
  gestures();
  focus();
  games();
  if (failures) { printf("%d failed\n", failures); return 1; }
  printf("logic ok\n");
  return 0;
}
