// Таймер фокусу: робота й перерва по колу. Чистий C++ без Arduino — у прошивці (focus.cpp) і в тестах.
#pragma once
#include <cstdint>

struct Focus {
  enum Phase { IDLE, WORK, BREAK };
  static const uint32_t ALERT_MS = 5000;      // стільки лампа сигналить про зміну відрізка

  Phase    phase = IDLE;
  bool     paused = false;
  uint32_t leftMs = 0;                        // скільки лишилось у поточному відрізку
  uint32_t totalMs = 0;                       // його повна тривалість
  uint16_t cycles = 0;                        // скільки робочих відрізків завершено

  bool running() const { return phase != IDLE && !paused; }
  bool alerting(uint32_t now) const { return alertUntil && now < alertUntil; }

  // Дотик: не запущений — старт роботи; іде — пауза; на паузі — далі
  void toggle(uint32_t now, uint32_t workMs) {
    if (phase == IDLE) begin(WORK, now, workMs);
    else { paused = !paused; tickAt = now; }
  }

  void reset() { phase = IDLE; paused = false; leftMs = totalMs = 0; alertUntil = 0; cycles = 0; }

  // Перейти до наступного відрізка, не чекаючи кінця поточного; пропущена робота в цикли не рахується
  void skip(uint32_t now, uint32_t workMs, uint32_t breakMs) {
    if (phase == IDLE) return;
    begin(phase == WORK ? BREAK : WORK, now, phase == WORK ? breakMs : workMs);
    alertUntil = now + ALERT_MS;
  }

  // Викликати часто. Повертає true в момент, коли відрізок закінчився сам і почався наступний.
  bool tick(uint32_t now, uint32_t workMs, uint32_t breakMs) {
    uint32_t passed = now - tickAt;
    tickAt = now;
    if (!running()) return false;
    if (passed < leftMs) { leftMs -= passed; return false; }
    if (phase == WORK) cycles++;
    begin(phase == WORK ? BREAK : WORK, now, phase == WORK ? breakMs : workMs);
    alertUntil = now + ALERT_MS;
    return true;
  }

 private:
  uint32_t tickAt = 0, alertUntil = 0;

  void begin(Phase next, uint32_t now, uint32_t durationMs) {
    phase = next;
    paused = false;
    leftMs = totalMs = durationMs;
    tickAt = now;
  }
};
