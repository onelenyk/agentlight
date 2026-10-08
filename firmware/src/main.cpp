// AgentLight: ESP32-C3 Super Mini + WS2812. Показує кольором статус AI-агента.
// Перший старт: точка доступу AgentLight-XXXX з порталом для налаштування WiFi (або по Bluetooth, web/setup.html).
// Далі: http://agentlight.local/ — сторінка, /api/status — REST, /mcp — MCP-сервер.
// Модулі описані в app.h, запити — у docs/firmware.md.
#include <WiFi.h>
#include "app.h"

void setup() {
  Serial.begin(115200);
  loadSettings();
  lightBegin();
  touchBegin();
  netBegin();
  bleBegin();
  apiBegin();
}

void loop() {
  pollNet();
  server.handleClient();
  pollTouch();
  pollBle();
  pollOta();
  render();

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[%lus] %s, стан: %s, ip: %s\n", lastLog / 1000, portal ? "портал" : "WiFi",
                  STATE_NAMES[aggregate()], (portal ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
  }
}
