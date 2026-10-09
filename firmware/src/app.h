// Спільні оголошення прошивки AgentLight. Кожен модуль — окремий .cpp:
//   settings — налаштування у флеші        agents — список агентів і загальний стан
//   light    — діоди й анімації            touch  — сенсор TTP223
//   net      — WiFi: збережені мережі, точка доступу з порталом
//   api      — веб-сервер, сторінка, REST  mcp    — MCP-сервер
//   hook     — прийом хуків агентів у їхньому власному форматі
//   ble      — налаштування WiFi по Bluetooth    ota    — оновлення прошивки по WiFi
//   timer    — таймер фокусу (логіка в focus.h, жести — у gesture.h: обидва перевіряються тестами на комп'ютері)
//   play     — ігри (логіка в games.h, теж під тестами)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include "focus.h"
#include "games.h"

#define FW_VERSION "0.12.0"
#define HOSTNAME   "agentlight"

const uint8_t FRAME_MS = 25;
const uint8_t MAX_BRIGHTNESS = 255;   // без стелі: корпус на 3 діоди, це до ~180 мА від USB

// П'ять станів лампи. Агент шле один із них; лампа показує найвищий серед усіх агентів (порядок = пріоритет).
//   idle    — нічого не відбувається: агентів немає або сесію закрито
//   done    — агент завершив хід успішно
//   busy    — агент працює
//   waiting — агент чекає від людини вводу або дозволу
//   error   — хід обірвався помилкою
enum State : int8_t { ST_IDLE, ST_DONE, ST_BUSY, ST_WAITING, ST_ERROR, ST_COUNT };
extern const char* const STATE_NAMES[ST_COUNT];

// Режими лампи: показувати стан агентів, просто світити, відлічувати час роботи й перерви, грати
// або бути Bluetooth-пультом для музики.
// Перемикаються зі сторінки або довгим утриманням сенсора: лампа по черзі показує колір кожного режиму.
enum Mode : uint8_t { MODE_AGENTS, MODE_LAMP, MODE_FOCUS, MODE_GAMES, MODE_MUSIC, MODE_COUNT };
extern const char* const MODE_NAMES[MODE_COUNT];
extern const uint32_t    MODE_COLORS[MODE_COUNT];

// Жести сенсора і дії, які на них можна призначити (окремо в кожному режимі)
enum Gesture : uint8_t { G_TAP, G_DOUBLE, G_HOLD, G_COUNT };
extern const char* const GESTURE_NAMES[G_COUNT];
enum Action : uint8_t { ACT_NONE, ACT_DISMISS, ACT_BRIGHT, ACT_TOGGLE, ACT_MODE, ACT_NEXT_ANIM, ACT_NEXT_COLOR,
                        ACT_SLEEP, ACT_SIGNAL, ACT_WEBHOOK, ACT_SPARK, ACT_FOCUS_TOGGLE, ACT_FOCUS_SKIP, ACT_FOCUS_RESET,
                        ACT_MEDIA_PLAY, ACT_MEDIA_NEXT, ACT_MEDIA_PREV, ACT_VOLUME_UP, ACT_VOLUME_DOWN,
                        ACT_KEY_F13, ACT_KEY_F14, ACT_KEY_F15, ACT_KEY_F16,   // клавіші, на які комп'ютер нічого не має: вішай свої команди
                        ACT_COUNT };    // нові дії — лише в кінець: номери зберігаються в налаштуваннях
extern const char* const ACTION_NAMES[ACT_COUNT];
bool actionFits(uint8_t action, Mode mode);     // чи має дія сенс у цьому режимі

// Анімації діодів; кожному стану в налаштуваннях призначена своя
enum Anim : uint8_t { AN_SOLID, AN_BREATHE, AN_BLINK, AN_SPIN, AN_COMET, AN_WAVE, AN_HEARTBEAT, AN_SPARKLE,
                      AN_PENDULUM, AN_FILL, AN_BEACON, AN_RAINBOW, AN_COUNT };   // rainbow ігнорує колір
extern const char* const ANIM_NAMES[AN_COUNT];

// ---- settings ----
// Налаштування зі сторінки лампи; зберігаються одним блоком. Нові поля додаються лише в кінець:
// loadSettings() читає й коротші блоки старих версій, а решту лишає типовою.
struct Settings {
  uint8_t  leds = 3;                  // скільки діодів світити (у корпусі три); не більше LED_COUNT
  uint16_t ttlBusyMin = 10;           // busy без оновлень: агент, мабуть, завис або вбитий; 0 — не гасне
  uint16_t ttlDoneMin = 5;            // зелений гасне сам
  uint16_t ttlAttentionMin = 120;     // waiting / error
  uint32_t color[ST_COUNT] = {0x000000, 0x00FF1E, 0xFF5A00, 0xFF0000, 0xFF0000};  // у порядку State
  uint8_t  tap = ACT_DISMISS;         // режим агентів: короткий дотик
  uint8_t  hold = ACT_BRIGHT;         // режим агентів: утримання
  uint8_t  anim[ST_COUNT] = {AN_SOLID, AN_SOLID, AN_BREATHE, AN_BLINK, AN_SOLID};  // у порядку State
  uint8_t  mode = MODE_AGENTS;
  uint8_t  dbl = ACT_MODE;            // режим агентів: подвійний дотик
  uint8_t  lampTouch[G_COUNT] = {ACT_TOGGLE, ACT_MODE, ACT_BRIGHT};   // режим лампи: дотик, подвійний, утримання
  uint8_t  lampAnim = AN_SOLID;
  uint16_t lampOffMin = 0;            // режим лампи: вимкнутись через стільки хвилин; 0 — світити завжди
  uint32_t lampColor = 0xFFB060;      // теплий білий
  uint16_t focusWorkMin = 25;         // режим фокусу: робота, хв
  uint16_t focusBreakMin = 5;         // режим фокусу: перерва, хв
  uint16_t sleepMin = 15;             // таймер сну: за стільки хвилин світло плавно згасає
  uint8_t  focusQuiet = 1;            // під час роботи не показувати, що агент чекає
  uint8_t  focusTouch[G_COUNT] = {ACT_FOCUS_TOGGLE, ACT_FOCUS_SKIP, ACT_FOCUS_RESET};
  uint8_t  gameSel = Games::REACTION; // режим ігор: вибрана гра
  uint8_t  diceKind = Games::YES_NO;  // що показує кубик
  uint8_t  musicTouch[G_COUNT] = {ACT_MEDIA_PLAY, ACT_MEDIA_NEXT, ACT_MEDIA_PREV};
  uint8_t  allowEnabled = 0;          // «Дозволити» (експериментально): вимкнено, доки власник не ввімкне
  uint8_t  allowCats = 3;             // що можна схвалювати з лампи: 1 — редагування файлів, 2 — команди, 4 — решта
};
// gameSel і diceKind лягли в хвіст вирівнювання, тож блок до ігор і після них однакового розміру (60);
// loadSettings() ці два поля просто перевіряє на допустимість.
static_assert(sizeof(Settings) == 68, "розмір Settings змінився: онови таблицю версій у loadSettings()");
extern String webhookUrl;             // адреса для дії «запит на адресу»; зберігається окремо від блоку налаштувань
void     setWebhook(const String& url);
uint8_t& touchAction(Mode mode, Gesture gesture);
void     setMode(Mode mode);
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
extern bool lightOff;                           // світло вимкнене дотиком або таймером; агенти рахуються далі
void lightBegin();
void lightSwitch(bool on);                      // вмикає (і заново запускає таймер режиму лампи) або вимикає
uint32_t lampSecondsLeft();                     // скільки лишилось до автовимкнення; 0 — таймера немає
void render();
void startRainbow(uint32_t ms);
void startPreview(State st, uint32_t ms);        // показати вигляд стану незалежно від агентів
void lightPick(int8_t mode);                    // під час вибору режиму утриманням: показати його колір; -1 — вибір закінчено
void startSleep();                              // режим лампи: плавно згасити за cfg.sleepMin хвилин
uint32_t sleepSecondsLeft();
void flash(uint16_t ms = 80);
void lightSolid(uint32_t rgb);                  // одразу, поза render(): під час оновлення loop() стоїть

// ---- touch ----
extern bool     touchRaw;
extern uint32_t touchCount, touchAt;
extern uint32_t signalCount, signalAt;          // дія «сигнал агентові»: скільки разів і коли востаннє
void touchBegin();
void pollTouch();
void runAction(uint8_t action, const char* gesture = "page");

// ---- play ----
extern Games games;
void playBegin();
void pollPlay();
void fillGame(JsonObject o);
void resetRecords();

// ---- timer ----
extern Focus focus;
void pollFocus();
void fillFocus(JsonObject o);

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
void   fireWebhook(const char* gesture);        // POST на webhookUrl; лише http, чекає щонайбільше 1,5 с

// ---- api ----
extern WebServer server;
void apiBegin();
void sendJson(int code, const JsonDocument& doc);
void fillStatus(JsonObject o);

// ---- mcp ----
void handleMcp();

// ---- hook ----
void hookBegin();                               // після apiBegin(): приймає хуки агентів на /hook/<інструмент>/<стан>

// ---- ble ----
void bleBegin();
void pollBle();
bool remoteConnected();                         // активний пристрій пульта підключений: клавіші дійдуть
int  remoteBonds();                             // зі скількома пристроями лампа спарована
void remoteSetActive(const String& id);         // кому слати клавіші
void remoteSetName(const String& id, const String& name);
void remoteForget(const String& id);            // забути один пристрій; порожній рядок — усі
void fillRemote(JsonObject o);
void remoteMedia(uint16_t usage);               // медіа-клавіша (HID Consumer): відтворення, трек, гучність
void remoteKey(uint8_t code);                   // звичайна клавіша (HID Keyboard)

// ---- allow ----
void allowBegin();                              // після apiBegin(): додає свої маршрути до сервера
void pollAllow();
bool allowAsking();                             // на лампі зараз запит дозволу від агента
bool allowPairing();                            // комп'ютер просить спаруватись
void allowTouch(bool yes);                      // відповідь власника: дотик — так, утримання — ні
void fillAllow(JsonObject o);

// ---- ota ----
bool otaReady();                                // пароль оновлення задано
void otaBegin();                                // після apiBegin(): додає свої маршрути до сервера
void pollOta();
