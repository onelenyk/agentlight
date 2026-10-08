// Діоди: один ланцюжок WS2812 на LED_PIN. Колір і анімація залежать від стану WiFi й агентів.
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "app.h"

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t rainbowUntil = 0, flashUntil = 0;
bool muted = false;

const uint8_t FLASH_MIN = 90;   // спалах на дотик видно й на найтьмянішій яскравості

void lightBegin() { strip.begin(); }
void startRainbow(uint32_t ms) { rainbowUntil = millis() + ms; }
void flash(uint16_t ms) { flashUntil = millis() + ms; }

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

void render() {
  static uint32_t last = 0;
  if (millis() - last < FRAME_MS) return;
  last = millis();
  float breathe = 0.3f + 0.7f * (0.5f + 0.5f * sinf(last / 1000.0f * PI));  // період 2 с
  bool  blinkOn = (last / 500) % 2;
  if (last < flashUntil) return fill(0xFFFFFF, 1.0f, max(brightness, FLASH_MIN));  // спалах: дотик почуто
  if (last < rainbowUntil) {                                             // тест із дебаг-меню
    for (uint8_t i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, i < cfg.leds ? Adafruit_NeoPixel::gamma32(Adafruit_NeoPixel::ColorHSV(last * 8 + i * 21845u)) : 0);
    return show();
  }
  if (portal) return fill(0x003CFF, breathe);                            // синій: режим налаштування
  if (WiFi.status() != WL_CONNECTED) return fill(0x9600FF, breathe);     // фіолетовий: немає WiFi
  State st = aggregate();
  if (muted) return fill(0, 0);
  switch (st) {
    case ST_BUSY:    return fill(cfg.color[st], breathe);
    case ST_WAITING: return fill(cfg.color[st], blinkOn ? 1.0f : 0.08f);
    case ST_ERROR:
    case ST_DONE:    return fill(cfg.color[st], 1.0f);
    default:         return fill(0, 0);
  }
}
