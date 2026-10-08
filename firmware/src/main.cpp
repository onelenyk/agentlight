// AgentLight: ESP32-C3 Super Mini + WS2812. Показує кольором статус AI-агента.
// Перший старт: точка доступу AgentLight-XXXX з captive portal для налаштування WiFi.
// Далі: http://agentlight.local/  — сторінка, /api/status — REST, /mcp — MCP-сервер (Streamable HTTP).
// WiFi можна налаштувати й по Bluetooth: сторінка web/setup.html у Chrome.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <NimBLEDevice.h>
#include "page.h"

const char*    HOSTNAME = "agentlight";
const uint8_t  MAX_BRIGHTNESS = 255;          // без стелі: корпус на 3 діоди, це до ~180 мА від USB
const uint8_t  FRAME_MS = 25;
const uint32_t CONNECT_TIMEOUT_MS = 20000;
const uint32_t PORTAL_RETRY_MS = 180000;      // у порталі зі збереженими мережами: перезапуск і нова спроба, якщо ніхто не підключений
const uint16_t TOUCH_DEBOUNCE_MS = 40;
const uint16_t TOUCH_HOLD_MS = 800;

// У порядку пріоритету: загальний колір — найвищий стан серед агентів
enum State : int8_t { ST_IDLE, ST_DONE, ST_BUSY, ST_WAITING, ST_ERROR, ST_COUNT };
const char* const STATE_NAMES[ST_COUNT] = {"idle", "done", "busy", "waiting", "error"};

struct Agent {
  bool     used = false;
  String   id, name, task, message;  // name — проєкт, task — останній запит юзера, message — що робить зараз
  State    state = ST_IDLE;
  uint32_t at = 0;       // останнє оновлення (для TTL)
  uint32_t since = 0;    // відколи в цьому стані
};
const uint8_t MAX_AGENTS = 8;
Agent agents[MAX_AGENTS];

Adafruit_NeoPixel stripA(LED_COUNT, LED_PIN_A, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(LED_COUNT, LED_PIN_B, NEO_GRB + NEO_KHZ800);
WebServer   server(80);
DNSServer   dns;
Preferences prefs;

String  apName;
// Збережені мережі WiFi (дім, офіс...): [{"s": назва, "p": пароль}], остання додана — перша
const uint8_t MAX_NETS = 5;
JsonDocument nets;
bool    portal = false;
uint8_t brightness = 60;

// Що робить дотик до сенсора
enum Action : uint8_t { ACT_NONE, ACT_DISMISS, ACT_BRIGHT, ACT_MUTE, ACT_COUNT };
const char* const ACTION_NAMES[ACT_COUNT] = {"none", "dismiss", "brightness", "mute"};

// Налаштування з дебаг-меню; зберігаються одним блоком, тож зміна складу структури скидає їх до типових
struct Settings {
  uint8_t  leds = LED_COUNT;          // скільки діодів світити на кожному виході
  uint16_t ttlBusyMin = 10;           // busy без оновлень: агент, мабуть, завис або вбитий; 0 — не гасне
  uint16_t ttlDoneMin = 5;            // зелений гасне сам
  uint16_t ttlAttentionMin = 120;     // waiting / error
  uint32_t color[ST_COUNT] = {0x000000, 0x00FF1E, 0xFF5A00, 0xFF0000, 0xFF0000};  // у порядку State
  uint8_t  tap = ACT_DISMISS;         // короткий дотик
  uint8_t  hold = ACT_BRIGHT;         // утримання
} cfg;

bool     muted = false;               // світло вимкнене дотиком; агенти рахуються далі
bool     touchRaw = false;
uint32_t touchCount = 0, touchAt = 0, rainbowUntil = 0, flashUntil = 0;

void saveSettings() { prefs.putBytes("cfg", &cfg, sizeof cfg); }

int8_t stateFromName(const char* name) {
  for (int8_t i = 0; i < ST_COUNT; i++)
    if (!strcmp(name, STATE_NAMES[i])) return i;
  return -1;
}

// Обрізає рядок до max байтів, не розрізаючи символ UTF-8
String clip(const char* text, size_t max) {
  if (strlen(text) <= max) return text;
  while (max > 0 && ((uint8_t)text[max] & 0xC0) == 0x80) max--;
  String out;
  out.concat(text, max);
  return out + "…";
}

// Знаходить або створює запис агента і ставить йому стан; для idle запис видаляється (повертає nullptr)
Agent* upsert(const String& id, State state) {
  Agent* slot = nullptr;
  for (auto& a : agents) if (a.used && a.id == id) slot = &a;
  if (state == ST_IDLE) { if (slot) slot->used = false; return nullptr; }
  if (!slot) {
    for (auto& a : agents) if (!a.used) { slot = &a; break; }
    if (!slot) {  // місць немає: витісняємо найстаріший запис
      slot = &agents[0];
      for (auto& a : agents) if (millis() - a.at > millis() - slot->at) slot = &a;
    }
    *slot = Agent();
    slot->used = true;
    slot->id = id;
  }
  if (slot->state != state) slot->since = millis();
  slot->state = state;
  slot->at = millis();
  return slot;
}

// Прибирає прострочені записи і повертає загальний стан
State aggregate() {
  State top = ST_IDLE;
  for (auto& a : agents) {
    if (!a.used) continue;
    uint32_t ttl = (a.state == ST_BUSY ? cfg.ttlBusyMin : a.state == ST_DONE ? cfg.ttlDoneMin : cfg.ttlAttentionMin) * 60000UL;
    if (ttl && millis() - a.at > ttl) { a.used = false; continue; }
    if (a.state > top) top = a.state;
  }
  return top;
}

void show() {
  stripA.setBrightness(brightness);
  stripB.setBrightness(brightness);
  stripA.show();
  stripB.show();
}

void fill(uint32_t rgb, float k) {
  uint32_t c = Adafruit_NeoPixel::Color((rgb >> 16 & 255) * k, (rgb >> 8 & 255) * k, (rgb & 255) * k);
  stripA.clear();
  stripB.clear();
  stripA.fill(c, 0, cfg.leds);
  stripB.fill(c, 0, cfg.leds);
  show();
}

void render() {
  static uint32_t last = 0;
  if (millis() - last < FRAME_MS) return;
  last = millis();
  float breathe = 0.3f + 0.7f * (0.5f + 0.5f * sinf(last / 1000.0f * PI));  // період 2 с
  bool  blinkOn = (last / 500) % 2;
  if (last < flashUntil) return fill(0xFFFFFF, 0.35f);                   // короткий спалах: дотик почуто
  if (last < rainbowUntil) {                                             // тест із дебаг-меню
    for (uint8_t i = 0; i < LED_COUNT; i++) {
      uint32_t c = i < cfg.leds ? Adafruit_NeoPixel::gamma32(Adafruit_NeoPixel::ColorHSV(last * 8 + i * 21845u)) : 0;
      stripA.setPixelColor(i, c);
      stripB.setPixelColor(i, c);
    }
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

void runAction(uint8_t action) {
  switch (action) {
    case ACT_DISMISS:   // «побачив»: прибирає все, крім агентів, що працюють
      for (auto& a : agents) if (a.used && a.state != ST_BUSY) a.used = false;
      break;
    case ACT_BRIGHT:    // по колу: тьмяно, середньо, повна
      brightness = brightness < 40 ? 40 : brightness < 120 ? 120 : brightness < MAX_BRIGHTNESS ? MAX_BRIGHTNESS : 15;
      prefs.putUChar("bright", brightness);
      break;
    case ACT_MUTE:
      muted = !muted;
      break;
  }
}

void pollTouch() {
  static bool     pressed = false, held = false;
  static uint32_t changedAt = 0;
  touchRaw = digitalRead(TOUCH_PIN);    // TTP223: HIGH, поки палець на сенсорі
  if (touchRaw != pressed && millis() - changedAt > TOUCH_DEBOUNCE_MS) {
    pressed = touchRaw;
    if (pressed) {
      touchCount++;
      touchAt = millis();
      flashUntil = millis() + 80;
    } else if (!held) {
      runAction(cfg.tap);
    }
    changedAt = millis();
    held = false;
  }
  if (pressed && !held && millis() - changedAt > TOUCH_HOLD_MS) {
    held = true;
    flashUntil = millis() + 80;
    runAction(cfg.hold);
  }
}

void sendJson(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void sendError(int code, const char* text) {
  JsonDocument doc;
  doc["error"] = text;
  sendJson(code, doc);
}

void fillStatus(JsonObject o) {
  o["state"] = STATE_NAMES[aggregate()];
  JsonArray list = o["agents"].to<JsonArray>();
  for (auto& a : agents) {
    if (!a.used) continue;
    JsonObject j = list.add<JsonObject>();
    j["id"] = a.id;
    j["state"] = STATE_NAMES[a.state];
    j["name"] = a.name;
    j["task"] = a.task;
    j["message"] = a.message;
    j["age"] = (millis() - a.at) / 1000;
    j["since"] = (millis() - a.since) / 1000;
  }
  o["brightness"] = brightness;
  o["muted"] = muted;
  JsonObject w = o["wifi"].to<JsonObject>();
  w["portal"] = portal;
  w["ap"] = apName;
  w["ssid"] = portal ? String() : WiFi.SSID();
  JsonArray saved = w["saved"].to<JsonArray>();
  for (JsonObject n : nets.as<JsonArray>()) saved.add(n["s"]);
  w["ip"] = (portal ? WiFi.softAPIP() : WiFi.localIP()).toString();
  w["rssi"] = WiFi.RSSI();
  w["host"] = String(HOSTNAME) + ".local";
}

// Спільне для REST і MCP: застосовує статус, повертає текст помилки або nullptr
const char* applyStatus(JsonVariantConst in) {
  int8_t st = stateFromName(in["state"] | "");
  if (st < 0) return "state must be one of: idle, busy, waiting, done, error";
  Agent* a = upsert(in["agent_id"] | "default", (State)st);
  if (!a) return nullptr;
  // name і task лишаються з попередніх оновлень, якщо їх не передали; message завжди про поточний момент
  if (in["name"].is<const char*>()) a->name = clip(in["name"], 40);
  if (in["task"].is<const char*>()) a->task = clip(in["task"], 240);
  a->message = clip(in["message"] | "", 160);
  return nullptr;
}

void handleStatusGet() {
  JsonDocument doc;
  fillStatus(doc.to<JsonObject>());
  sendJson(200, doc);
}

// Приймає JSON-тіло або параметри запиту: curl -X POST 'http://agentlight.local/api/status?state=busy'
void handleStatusPost() {
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

void restartSoon();

const char* resetReason() {
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

String hexColor(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof buf, "#%06x", (unsigned)(c & 0xFFFFFF));
  return buf;
}

void fillDebug(JsonObject o) {
  JsonObject d = o["device"].to<JsonObject>();
  d["uptime"] = millis() / 1000;
  d["reset"] = resetReason();
  d["build"] = __DATE__ " " __TIME__;
  d["chip"] = ESP.getChipModel();
  d["mac"] = WiFi.macAddress();
  d["heap"] = ESP.getFreeHeap();
  d["heap_min"] = ESP.getMinFreeHeap();
  d["led_pins"] = String(LED_PIN_A) + ", " + LED_PIN_B;
  d["led_max"] = LED_COUNT;
  d["touch_pin"] = TOUCH_PIN;
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
  for (int8_t i = ST_DONE; i < ST_COUNT; i++) c["colors"][STATE_NAMES[i]] = hexColor(cfg.color[i]);
  c["tap"] = ACTION_NAMES[cfg.tap];
  c["hold"] = ACTION_NAMES[cfg.hold];
  JsonArray acts = o["actions"].to<JsonArray>();
  for (const char* n : ACTION_NAMES) acts.add(n);
}

void handleDebug() {
  JsonDocument doc;
  fillStatus(doc.to<JsonObject>());
  fillDebug(doc.as<JsonObject>());
  sendJson(200, doc);
}

uint8_t actionFromName(const char* name, uint8_t fallback) {
  for (uint8_t i = 0; i < ACT_COUNT; i++)
    if (!strcmp(name, ACTION_NAMES[i])) return i;
  return fallback;
}

// Приймає будь-яку підмножину налаштувань; чого не передали — лишається як було
void handleConfig() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) return sendError(400, "bad json");
  brightness = constrain(in["brightness"] | (int)brightness, 5, (int)MAX_BRIGHTNESS);
  prefs.putUChar("bright", brightness);
  cfg.leds = constrain(in["leds"] | (int)cfg.leds, 1, LED_COUNT);
  cfg.ttlBusyMin = constrain(in["ttl_busy"] | (int)cfg.ttlBusyMin, 0, 1440);
  cfg.ttlDoneMin = constrain(in["ttl_done"] | (int)cfg.ttlDoneMin, 0, 1440);
  cfg.ttlAttentionMin = constrain(in["ttl_attention"] | (int)cfg.ttlAttentionMin, 0, 1440);
  for (int8_t i = ST_DONE; i < ST_COUNT; i++) {
    const char* hex = in["colors"][STATE_NAMES[i]] | "";
    if (strlen(hex) == 7 && hex[0] == '#') cfg.color[i] = strtoul(hex + 1, nullptr, 16);
  }
  cfg.tap = actionFromName(in["tap"] | "", cfg.tap);
  cfg.hold = actionFromName(in["hold"] | "", cfg.hold);
  saveSettings();
  handleDebug();
}

// Кнопки дебаг-меню
void handleAction() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  String action = in["action"] | "";
  if (action == "reboot") return restartSoon();
  if (action == "rainbow") rainbowUntil = millis() + 10000;
  else if (action == "clear") { for (auto& a : agents) a.used = false; }
  else if (action == "unmute") muted = false;
  else if (action == "defaults") { cfg = Settings(); saveSettings(); }
  else return sendError(400, "unknown action");
  handleDebug();
}

void handleScan() {
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    bool seen = ssid.isEmpty();
    for (JsonObject o : list) if (ssid == (o["ssid"] | "")) seen = true;
    if (seen) continue;
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  sendJson(200, doc);
}

void restartSoon() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void saveNets() {
  String out;
  serializeJson(nets, out);
  prefs.putString("nets", out);
}

void forgetNet(const char* ssid) {
  JsonArray list = nets.as<JsonArray>();
  for (size_t i = list.size(); i-- > 0;)
    if (!strcmp(list[i]["s"] | "", ssid)) list.remove(i);
}

// Додає мережу до збережених або оновлює її пароль; остання додана стає першою
void addNet(const char* ssid, const char* pass) {
  forgetNet(ssid);
  JsonDocument next;
  JsonObject first = next.add<JsonObject>();
  first["s"] = ssid;
  first["p"] = pass;
  for (JsonObject n : nets.as<JsonArray>())
    if (next.size() < MAX_NETS) next.add(n);
  nets = next;
  saveNets();
}

void handleWifiSave() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !strlen(in["ssid"] | "")) return sendError(400, "ssid required");
  addNet(in["ssid"] | "", in["pass"] | "");
  restartSoon();
}

// Забуває одну мережу (ssid у тілі) або всі
void handleWifiReset() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  if (strlen(in["ssid"] | "")) forgetNet(in["ssid"]);
  else nets.to<JsonArray>();
  saveNets();
  restartSoon();
}

// ---- MCP: Streamable HTTP без сесій і без SSE, кожна відповідь — звичайний JSON ----

const char MCP_TOOLS[] PROGMEM = R"JSON({"tools":[
{"name":"set_status","description":"Set the colour of the physical agent status light. Call with state=busy when you start working, state=waiting when you need input or approval from the user, state=done when the task is finished, state=error on failure, state=idle to switch the light off.",
"inputSchema":{"type":"object","properties":{
"state":{"type":"string","enum":["idle","busy","waiting","done","error"]},
"agent_id":{"type":"string","description":"Stable name of this agent or session, so several agents can share one light. Default: \"default\"."},
"name":{"type":"string","description":"Optional human-readable name of the agent or project."},
"task":{"type":"string","description":"Optional one-line summary of the task you are working on. Kept until you send a new one."},
"message":{"type":"string","description":"Optional short note about what you are doing right now."}},
"required":["state"]}},
{"name":"get_status","description":"Get the current state of the agent status light and the list of agents that reported a status.",
"inputSchema":{"type":"object","properties":{}}}
]})JSON";

void handleMcp() {
  JsonDocument req, res;
  res["jsonrpc"] = "2.0";
  if (deserializeJson(req, server.arg("plain")) || !req.is<JsonObject>()) {
    res["id"] = nullptr;
    res["error"]["code"] = -32700;
    res["error"]["message"] = "Parse error";
    return sendJson(400, res);
  }
  if (req["id"].isNull()) return server.send(202, "text/plain", "");  // notifications/* і відповіді клієнта
  res["id"] = req["id"];
  String method = req["method"] | "";

  if (method == "initialize") {
    JsonObject r = res["result"].to<JsonObject>();
    r["protocolVersion"] = req["params"]["protocolVersion"] | "2025-03-26";
    r["capabilities"]["tools"].to<JsonObject>();
    r["serverInfo"]["name"] = "agentlight";
    r["serverInfo"]["version"] = "0.1.0";
    r["instructions"] = "Physical status light. Report your state with set_status: busy while working, waiting when you need the user, done when finished.";
  } else if (method == "ping") {
    res["result"].to<JsonObject>();
  } else if (method == "tools/list") {
    res["result"] = serialized(FPSTR(MCP_TOOLS));
  } else if (method == "tools/call") {
    String name = req["params"]["name"] | "";
    const char* err = nullptr;
    if (name == "set_status") err = applyStatus(req["params"]["arguments"]);
    else if (name != "get_status") err = "unknown tool";
    String text = err;
    if (!err) {
      JsonDocument st;
      fillStatus(st.to<JsonObject>());
      st.remove("wifi");
      serializeJson(st, text);
    }
    JsonObject r = res["result"].to<JsonObject>();
    JsonObject c = r["content"].add<JsonObject>();
    c["type"] = "text";
    c["text"] = text;
    r["isError"] = err != nullptr;
  } else {
    res["error"]["code"] = -32601;
    res["error"]["message"] = "Method not found";
  }
  sendJson(200, res);
}

// ---- WiFi ----

bool tryNet(const char* ssid, const char* pass) {
  Serial.printf("WiFi: підключаюсь до %s\n", ssid);
  WiFi.begin(ssid, pass);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // Super Mini: на повній потужності антена часто не дає підключитись
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT_MS) {
    render();
    delay(FRAME_MS);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Шукає в ефірі збережені мережі й підключається до найсильнішої з них: удома — до домашньої, в офісі — до офісної
bool connectSaved() {
  JsonArray list = nets.as<JsonArray>();
  if (!list.size()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  int found = WiFi.scanNetworks();
  JsonObject best;
  int bestRssi = -1000;
  for (JsonObject n : list)
    for (int i = 0; i < found; i++)
      if (WiFi.SSID(i) == (n["s"] | "") && WiFi.RSSI(i) > bestRssi) { best = n; bestRssi = WiFi.RSSI(i); }
  WiFi.scanDelete();
  if (!best.isNull()) return tryNet(best["s"], best["p"]);
  Serial.println("WiFi: жодної збереженої мережі поруч немає");
  // прихована мережа у скануванні не видна: якщо збережена лише одна, пробуємо її наосліп
  return list.size() == 1 && tryNet(list[0]["s"], list[0]["p"]);
}

// ---- Bluetooth: налаштування WiFi без підключення до точки доступу. Клієнт — web/setup.html у Chrome ----
// status (читання): стан лампи і мережі. scan (читання): результат останнього пошуку мереж.
// cmd (запис): {"cmd":"scan"} | {"cmd":"add","ssid":..,"pass":..} | {"cmd":"forget","ssid":..} | {"cmd":"forget"}

#define BLE_SERVICE "a9e10001-7c1e-4b6f-9d2a-41676e744c69"
#define BLE_STATUS  "a9e10002-7c1e-4b6f-9d2a-41676e744c69"
#define BLE_SCAN    "a9e10003-7c1e-4b6f-9d2a-41676e744c69"
#define BLE_CMD     "a9e10004-7c1e-4b6f-9d2a-41676e744c69"
const size_t BLE_VALUE_MAX = 500;      // стеля довжини значення характеристики — 512 байтів

NimBLECharacteristic* bleStatus = nullptr;
NimBLECharacteristic* bleScan = nullptr;
String        bleCmd;                  // команда від сторінки; виконується в loop(), а не в потоці Bluetooth
volatile bool bleCmdReady = false;
uint32_t      bleScanSeq = 0;

class BleCmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    if (bleCmdReady) return;
    bleCmd = c->getValue().c_str();
    bleCmdReady = true;
  }
};

class BleServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer*) override { NimBLEDevice::startAdvertising(); }
};

void startBle() {
  NimBLEDevice::init(apName.c_str());
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new BleServerCallbacks());
  NimBLEService* svc = srv->createService(BLE_SERVICE);
  bleStatus = svc->createCharacteristic(BLE_STATUS, NIMBLE_PROPERTY::READ);
  bleScan = svc->createCharacteristic(BLE_SCAN, NIMBLE_PROPERTY::READ);
  bleScan->setValue(std::string("{\"seq\":0,\"list\":[]}"));
  svc->createCharacteristic(BLE_CMD, NIMBLE_PROPERTY::WRITE)->setCallbacks(new BleCmdCallbacks());
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE);
  adv->setScanResponse(true);          // назва не влазить поруч із UUID сервісу, їде у відповіді на сканування
  adv->start();
}

void bleScanNets() {
  JsonDocument doc;
  doc["seq"] = ++bleScanSeq;
  JsonArray list = doc["list"].to<JsonArray>();
  int n = WiFi.scanNetworks();          // відсортовано за силою сигналу
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    bool seen = ssid.isEmpty();
    for (JsonVariant v : list) if (ssid == (v | "")) seen = true;
    if (seen) continue;
    list.add(ssid);
    if (measureJson(doc) > BLE_VALUE_MAX) { list.remove(list.size() - 1); break; }
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  bleScan->setValue(std::string(out.c_str()));   // саме std::string: з const char* бібліотека скопіювала б вказівник
}

void pollBle() {
  static uint32_t statusAt = 0;
  if (millis() - statusAt > 1000) {
    statusAt = millis();
    JsonDocument doc;
    doc["name"] = apName;
    doc["state"] = STATE_NAMES[aggregate()];
    doc["portal"] = portal;
    doc["ssid"] = portal ? String() : WiFi.SSID();
    doc["ip"] = portal ? String() : WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    JsonArray saved = doc["saved"].to<JsonArray>();
    for (JsonObject n : nets.as<JsonArray>()) saved.add(n["s"]);
    String out;
    serializeJson(doc, out);
    bleStatus->setValue(std::string(out.c_str(), min((size_t)out.length(), BLE_VALUE_MAX)));
  }
  if (!bleCmdReady) return;
  JsonDocument in;
  DeserializationError bad = deserializeJson(in, bleCmd);
  bleCmdReady = false;
  if (bad) return;
  String cmd = in["cmd"] | "";
  const char* ssid = in["ssid"] | "";
  if (cmd == "scan") return bleScanNets();
  if (cmd == "add" && strlen(ssid)) addNet(ssid, in["pass"] | "");
  else if (cmd == "forget" && strlen(ssid)) { forgetNet(ssid); saveNets(); }
  else if (cmd == "forget") { nets.to<JsonArray>(); saveNets(); }
  else return;
  delay(500);                           // сторінка встигає отримати підтвердження запису
  ESP.restart();
}

void startPortal() {
  portal = true;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);  // STA лишається для сканування мереж
  WiFi.softAP(apName.c_str());
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  dns.start(53, "*", WiFi.softAPIP());  // будь-який домен веде на нас: телефон сам відкриє сторінку
  Serial.printf("Портал: точка доступу %s, http://%s/\n", apName.c_str(), WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  stripA.begin();
  stripB.begin();
  prefs.begin("agentlight");
  brightness = prefs.getUChar("bright", brightness);
  if (prefs.isKey("cfg") && prefs.getBytesLength("cfg") == sizeof cfg) prefs.getBytes("cfg", &cfg, sizeof cfg);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);   // без модуля пін не «плаває» і дотиків не вигадує
  if (deserializeJson(nets, prefs.getString("nets", "[]")) || !nets.is<JsonArray>()) nets.to<JsonArray>();
  if (prefs.isKey("ssid")) {   // перехід зі старої прошивки, де мережа була одна
    JsonObject n = nets.add<JsonObject>();
    n["s"] = prefs.getString("ssid", "");
    n["p"] = prefs.getString("pass", "");
    saveNets();
    prefs.remove("ssid");
    prefs.remove("pass");
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof suffix, "%02X%02X", mac[4], mac[5]);
  apName = String("AgentLight-") + suffix;

  if (connectSaved()) {
    Serial.printf("WiFi: готово, http://%s/ або http://%s.local/\n", WiFi.localIP().toString().c_str(), HOSTNAME);
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  } else {
    startPortal();
  }

  startBle();

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", PAGE_HTML); });
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
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

void loop() {
  if (portal) {
    dns.processNextRequest();
    if (nets.size() && millis() > PORTAL_RETRY_MS && WiFi.softAPgetStationNum() == 0) ESP.restart();
  }
  server.handleClient();
  pollTouch();
  pollBle();
  render();

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[%lus] %s, стан: %s, ip: %s\n", lastLog / 1000, portal ? "портал" : "WiFi",
                  STATE_NAMES[aggregate()], (portal ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
  }
}
