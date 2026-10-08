// Тестова прошивка: перевірка діодів і сенсора TTP223 без WiFi.
// Діоди вимкнені. Поки палець на сенсорі — світять випадковим кольором, кожен дотик — іншим.
// Залити: pio run -e touchtest -t upload      Повернути основну: pio run -e c3_supermini -t upload
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

const uint8_t  BRIGHTNESS = 120;
const uint16_t DEBOUNCE_MS = 40;

Adafruit_NeoPixel stripA(LED_COUNT, LED_PIN_A, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(LED_COUNT, LED_PIN_B, NEO_GRB + NEO_KHZ800);

void fill(uint32_t c) {
  stripA.fill(c);
  stripB.fill(c);
  stripA.show();
  stripB.show();
}

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);   // без модуля пін не «плаває» і дотиків не вигадує
  stripA.begin();
  stripB.begin();
  stripA.setBrightness(BRIGHTNESS);
  stripB.setBrightness(BRIGHTNESS);
  randomSeed(esp_random());
  fill(0);
}

void loop() {
  static bool     pressed = false;
  static uint16_t hue = 0;
  static uint32_t changedAt = 0;
  bool now = digitalRead(TOUCH_PIN);    // TTP223: HIGH, поки палець на сенсорі
  if (now == pressed || millis() - changedAt <= DEBOUNCE_MS) return;
  pressed = now;
  changedAt = millis();
  if (pressed) {
    hue += 8000 + random(49536);        // щонайменше 1/8 кола від попереднього: колір помітно інший
    fill(Adafruit_NeoPixel::gamma32(Adafruit_NeoPixel::ColorHSV(hue)));
    Serial.printf("дотик: відтінок %u°\n", hue * 360u / 65536u);
  } else {
    fill(0);
    Serial.println("відпущено: вимкнено");
  }
}
