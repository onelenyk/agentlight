// Веб-сервер: сторінка лампи і REST. Опис запитів — у docs/firmware.md.
#include <WiFi.h>
#include "app.h"
#include "page_gz.h"

WebServer server(80);

void sendJson(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void sendError(int code, const char* text) {
  JsonDocument doc;
  doc["error"] = text;
  sendJson(code, doc);
}

static void restartSoon() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void fillStatus(JsonObject o) {
  o["state"] = STATE_NAMES[aggregate()];
  fillAgents(o["agents"].to<JsonArray>());
  o["brightness"] = brightness;
  o["muted"] = muted;
  fillWifi(o["wifi"].to<JsonObject>());
}

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "увімкнення живлення";
    case ESP_RST_SW:       return "програмний перезапуск";
    case ESP_RST_PANIC:    return "збій прошивки";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "сторожовий таймер";
    case ESP_RST_BROWNOUT: return "просідання живлення";
    default:               return "інше";
  }
}

static void fillDebug(JsonObject o) {
  JsonObject d = o["device"].to<JsonObject>();
  d["version"] = FW_VERSION;
  d["build"] = __DATE__ " " __TIME__;
  d["uptime"] = millis() / 1000;
  d["reset"] = resetReason();
  d["chip"] = ESP.getChipModel();
  d["mac"] = WiFi.macAddress();
  d["heap"] = ESP.getFreeHeap();
  d["heap_min"] = ESP.getMinFreeHeap();
  d["led_pin"] = LED_PIN;
  d["led_max"] = LED_COUNT;
  d["touch_pin"] = TOUCH_PIN;
  d["ota_ready"] = otaReady();
  JsonObject t = o["touch"].to<JsonObject>();
  t["raw"] = touchRaw;
  t["count"] = touchCount;
  if (touchCount) t["ago"] = (millis() - touchAt) / 1000;
  JsonObject c = o["settings"].to<JsonObject>();
  c["brightness"] = brightness;
  c["leds"] = cfg.leds;
  c["ttl_busy"] = cfg.ttlBusyMin;
  c["ttl_done"] = cfg.ttlDoneMin;
  c["ttl_attention"] = cfg.ttlAttentionMin;
  for (int8_t i = ST_IDLE; i < ST_COUNT; i++) {
    char hex[8];
    snprintf(hex, sizeof hex, "#%06x", (unsigned)(cfg.color[i] & 0xFFFFFF));
    c["colors"][STATE_NAMES[i]] = hex;
  }
  for (int8_t i = ST_IDLE; i < ST_COUNT; i++) c["anims"][STATE_NAMES[i]] = ANIM_NAMES[cfg.anim[i]];
  c["tap"] = ACTION_NAMES[cfg.tap];
  c["hold"] = ACTION_NAMES[cfg.hold];
  JsonArray acts = o["actions"].to<JsonArray>();
  for (const char* n : ACTION_NAMES) acts.add(n);
  JsonArray anims = o["animations"].to<JsonArray>();
  for (const char* n : ANIM_NAMES) anims.add(n);
}

static void handleStatusGet() {
  JsonDocument doc;
  fillStatus(doc.to<JsonObject>());
  sendJson(200, doc);
}

// Приймає JSON-тіло або параметри запиту: curl -X POST 'http://agentlight.local/api/status?state=busy'
static void handleStatusPost() {
  JsonDocument in;
  String body = server.arg("plain");
  if (body.startsWith("{")) {
    if (deserializeJson(in, body)) return sendError(400, "bad json");
  } else {
    for (const char* key : {"state", "agent_id", "name", "task", "message"})
      if (server.hasArg(key)) in[key] = server.arg(key);
  }
  if (const char* err = applyStatus(in)) return sendError(400, err);
  handleStatusGet();
}

static void handleDebug() {
  JsonDocument doc;
  fillStatus(doc.to<JsonObject>());
  fillDebug(doc.as<JsonObject>());
  sendJson(200, doc);
}

// Індекс назви у списку або fallback, якщо такої немає
static uint8_t indexOf(const char* name, const char* const* names, uint8_t count, uint8_t fallback) {
  for (uint8_t i = 0; i < count; i++)
    if (!strcmp(name, names[i])) return i;
  return fallback;
}

// Приймає будь-яку підмножину налаштувань; чого не передали — лишається як було
static void handleConfig() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) return sendError(400, "bad json");
  setBrightness(in["brightness"] | (int)brightness);
  cfg.leds = constrain(in["leds"] | (int)cfg.leds, 1, LED_COUNT);
  cfg.ttlBusyMin = constrain(in["ttl_busy"] | (int)cfg.ttlBusyMin, 0, 1440);
  cfg.ttlDoneMin = constrain(in["ttl_done"] | (int)cfg.ttlDoneMin, 0, 1440);
  cfg.ttlAttentionMin = constrain(in["ttl_attention"] | (int)cfg.ttlAttentionMin, 0, 1440);
  for (int8_t i = ST_IDLE; i < ST_COUNT; i++) {
    const char* hex = in["colors"][STATE_NAMES[i]] | "";
    if (strlen(hex) == 7 && hex[0] == '#') cfg.color[i] = strtoul(hex + 1, nullptr, 16);
    cfg.anim[i] = indexOf(in["anims"][STATE_NAMES[i]] | "", ANIM_NAMES, AN_COUNT, cfg.anim[i]);
  }
  cfg.tap = indexOf(in["tap"] | "", ACTION_NAMES, ACT_COUNT, cfg.tap);
  cfg.hold = indexOf(in["hold"] | "", ACTION_NAMES, ACT_COUNT, cfg.hold);
  saveSettings();
  handleDebug();
}

// Кнопки дебаг-меню
static void handleAction() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  String action = in["action"] | "";
  if (action == "reboot") return restartSoon();
  if (action == "rainbow") startRainbow(10000);
  else if (action == "preview") {   // {"action":"preview","state":"waiting"}: 6 с показує вигляд цього стану
    uint8_t st = indexOf(in["state"] | "", STATE_NAMES, ST_COUNT, ST_COUNT);
    if (st == ST_COUNT) return sendError(400, "unknown state");
    startPreview((State)st, 6000);
  }
  else if (action == "clear") clearAgents(false);
  else if (action == "unmute") muted = false;
  else if (action == "defaults") { cfg = Settings(); saveSettings(); }
  else return sendError(400, "unknown action");
  handleDebug();
}

static void handleScan() {
  Net found[20];
  size_t count = scanNets(found, 20);
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = found[i].ssid;
    o["rssi"] = found[i].rssi;
  }
  sendJson(200, doc);
}

static void handleWifiSave() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !strlen(in["ssid"] | "")) return sendError(400, "ssid required");
  addNet(in["ssid"] | "", in["pass"] | "");
  restartSoon();
}

// Забуває одну мережу (ssid у тілі) або всі
static void handleWifiReset() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  forgetNet(in["ssid"] | "");
  restartSoon();
}

void apiBegin() {
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)PAGE_GZ, PAGE_GZ_LEN);
  });
  server.on("/api/status", HTTP_GET, handleStatusGet);
  server.on("/api/status", HTTP_POST, handleStatusPost);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/debug", HTTP_GET, handleDebug);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/reset", HTTP_POST, handleWifiReset);
  server.on("/mcp", HTTP_POST, handleMcp);
  server.on("/mcp", HTTP_GET, [] { server.send(405, "text/plain", "Method Not Allowed"); });  // SSE-потоку немає
  server.onNotFound([] {
    if (!portal) return server.send(404, "text/plain", "Not found");
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");   // портал: будь-яка адреса веде на сторінку
    server.send(302, "text/plain", "");
  });
  otaBegin();
  server.begin();
}
