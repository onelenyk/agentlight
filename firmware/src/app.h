// Спільні оголошення прошивки AgentLight. Кожен модуль — окремий .cpp:
//   settings — налаштування у флеші        agents — список агентів і загальний стан
//   light    — діоди й анімації            touch  — сенсор TTP223
//   net      — WiFi: збережені мережі, точка доступу з порталом
//   api      — веб-сервер, сторінка, REST  mcp    — MCP-сервер
//   ble      — налаштування WiFi по Bluetooth    ota    — оновлення прошивки по WiFi
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

#define FW_VERSION "0.3.0"
#define HOSTNAME   "agentlight"

const uint8_t FRAME_MS = 25;
const uint8_t MAX_BRIGHTNESS = 255;   // без стелі: корпус на 3 діоди, це до ~180 мА від USB

// У порядку пріоритету: загальний колір — найвищий стан серед агентів
enum State : int8_t { ST_IDLE, ST_DONE, ST_BUSY, ST_WAITING, ST_ERROR, ST_COUNT };
extern const char* const STATE_NAMES[ST_COUNT];

// Що робить дотик до сенсора
enum Action : uint8_t { ACT_NONE, ACT_DISMISS, ACT_BRIGHT, ACT_MUTE, ACT_COUNT };
extern const char* const ACTION_NAMES[ACT_COUNT];

// ---- settings ----
// Налаштування з дебаг-меню; зберігаються одним блоком, тож зміна складу структури скидає їх до типових
struct Settings {
  uint8_t  leds = LED_COUNT;          // скільки діодів світити
  uint16_t ttlBusyMin = 10;           // busy без оновлень: агент, мабуть, завис або вбитий; 0 — не гасне
  uint16_t ttlDoneMin = 5;            // зелений гасне сам
  uint16_t ttlAttentionMin = 120;     // waiting / error
  uint32_t color[ST_COUNT] = {0x000000, 0x00FF1E, 0xFF5A00, 0xFF0000, 0xFF0000};  // у порядку State
  uint8_t  tap = ACT_DISMISS;         // короткий дотик
  uint8_t  hold = ACT_BRIGHT;         // утримання
};
extern Settings    cfg;
extern uint8_t     brightness;
extern Preferences prefs;
void loadSettings();
void saveSettings();
void setBrightness(int value);

// ---- agents ----
State       aggregate();                        // прибирає прострочені записи і повертає загальний стан
const char* applyStatus(JsonVariantConst in);   // спільне для REST і MCP; повертає текст помилки або nullptr
void        fillAgents(JsonArray list);
void        clearAgents(bool keepBusy);

// ---- light ----
extern bool muted;                              // світло вимкнене дотиком; агенти рахуються далі
void lightBegin();
void render();
void startRainbow(uint32_t ms);
void flash(uint16_t ms = 80);
void lightSolid(uint32_t rgb);                  // одразу, поза render(): під час оновлення loop() стоїть

// ---- touch ----
extern bool     touchRaw;
extern uint32_t touchCount, touchAt;
void touchBegin();
void pollTouch();

// ---- net ----
struct Net { String ssid; int rssi; };
extern String apName;                           // AgentLight-XXXX: назва точки доступу і пристрою Bluetooth
extern bool   portal;                           // точка доступу з порталом замість підключення до WiFi
void   netBegin();
void   pollNet();
void   addNet(const char* ssid, const char* pass);
void   forgetNet(const char* ssid);             // порожній рядок — забути всі
size_t scanNets(Net* out, size_t max);          // мережі поруч без повторів, найсильніші перші
void   fillWifi(JsonObject w);

// ---- api ----
extern WebServer server;
void apiBegin();
void sendJson(int code, const JsonDocument& doc);
void fillStatus(JsonObject o);

// ---- mcp ----
void handleMcp();

// ---- ble ----
void bleBegin();
void pollBle();

// ---- ota ----
bool otaReady();                                // пароль оновлення задано
void otaBegin();                                // після apiBegin(): додає свої маршрути до сервера
void pollOta();
