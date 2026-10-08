// Діоди: один ланцюжок WS2812 на LED_PIN. Колір і анімація залежать від стану WiFi й агентів.
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "app.h"

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t rainbowUntil = 0, flashUntil = 0, previewUntil = 0;
static State    previewState = ST_IDLE;
bool muted = false;

const uint8_t FLASH_MIN = 90;   // спалах на дотик видно й на найтьмянішій яскравості

void lightBegin() { strip.begin(); }
void startRainbow(uint32_t ms) { rainbowUntil = millis() + ms; }
void flash(uint16_t ms) { flashUntil = millis() + ms; }
void startPreview(State st, uint32_t ms) { previewState = st; previewUntil = millis() + ms; }

static void show(uint8_t level = brightness) {
  strip.setBrightness(level);
  strip.show();
}

static void fill(uint32_t rgb, float k, uint8_t level = brightness) {
  strip.clear();
  strip.fill(Adafruit_NeoPixel::Color((rgb >> 16 & 255) * k, (rgb >> 8 & 255) * k, (rgb & 255) * k), 0, cfg.leds);
  show(level);
}

void lightSolid(uint32_t rgb) { fill(rgb, 0.5f); }

static float triangle(float x) { return x < 0 || x > 1 ? 0 : 1 - fabsf(2 * x - 1); }   // 0 → 1 → 0 на відрізку [0, 1]

// Яскравість діода i з n у момент t (мс), від 0 до 1. Діоди стоять по колу, тому «по колу» — це по індексу.
static float animLevel(uint8_t anim, uint32_t t, uint8_t i, uint8_t n) {
  static float spark[LED_COUNT];
  const float dim = 0.06f;
  switch (anim) {
    case AN_BREATHE:   return 0.3f + 0.7f * (0.5f + 0.5f * sinf(t / 1000.0f * PI));          // період 2 с
    case AN_BLINK:     return (t / 500) % 2 ? 1.0f : 0.08f;
    case AN_SPIN:      return (t / 250) % n == i ? 1.0f : dim;                               // один вогник біжить по колу
    case AN_COMET: {                                                                         // голова з хвостом, плавно
      float behind = fmodf(fmodf(t / 300.0f, n) - i + n, n) / n;
      return max(dim, (1 - behind) * (1 - behind) * (1 - behind));
    }
    case AN_WAVE:      return 0.12f + 0.88f * (0.5f + 0.5f * sinf(t / 1000.0f * 1.4f * PI - i * 2 * PI / n));
    case AN_HEARTBEAT: {                                                                     // два удари і пауза
      float ms = t % 1200;
      return 0.1f + 0.9f * max(triangle(ms / 180.0f), 0.7f * triangle((ms - 260) / 200.0f));
    }
    case AN_SPARKLE:                                                                         // випадкові спалахи, що згасають
      if (esp_random() % 100 < 6) spark[i] = 1; else spark[i] *= 0.88f;
      return 0.1f + 0.9f * spark[i];
    case AN_PENDULUM: {                                                                      // туди й назад
      uint8_t steps = n > 1 ? 2 * n - 2 : 1, step = (t / 220) % steps;
      return (step < n ? step : steps - step) == i ? 1.0f : dim;
    }
    case AN_FILL:      return i < (t / 350) % (n + 2) ? 1.0f : dim;                          // загоряються по одному, потім усі гаснуть
    case AN_BEACON: {                                                                        // два короткі спалахи і пауза
      uint16_t ms = t % 1400;
      return ms < 90 || (ms >= 180 && ms < 270) ? 1.0f : 0.05f;
    }
    default:           return 1.0f;
  }
}

static void showState(State st, uint32_t t) {
  uint32_t rgb = cfg.color[st];
  strip.clear();
  for (uint8_t i = 0; i < cfg.leds; i++) {
    float k = animLevel(cfg.anim[st], t, i, cfg.leds);
    strip.setPixelColor(i, (uint8_t)((rgb >> 16 & 255) * k), (uint8_t)((rgb >> 8 & 255) * k), (uint8_t)((rgb & 255) * k));
  }
  show();
}

void render() {
  static uint32_t last = 0;
  if (millis() - last < FRAME_MS) return;
  last = millis();
  float breathe = 0.3f + 0.7f * (0.5f + 0.5f * sinf(last / 1000.0f * PI));  // період 2 с
  if (last < flashUntil) return fill(0xFFFFFF, 1.0f, max(brightness, FLASH_MIN));  // спалах: дотик почуто
  if (last < rainbowUntil) {                                             // тест із дебаг-меню
    for (uint8_t i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, i < cfg.leds ? Adafruit_NeoPixel::gamma32(Adafruit_NeoPixel::ColorHSV(last * 8 + i * 21845u)) : 0);
    return show();
  }
  if (last < previewUntil) return showState(previewState, last);         // проба анімації зі сторінки
  if (portal) return fill(0x003CFF, breathe);                            // синій: режим налаштування
  if (WiFi.status() != WL_CONNECTED) return fill(0x9600FF, breathe);     // фіолетовий: немає WiFi
  State st = aggregate();
  if (muted || st == ST_IDLE) return fill(0, 0);
  showState(st, last);
}
