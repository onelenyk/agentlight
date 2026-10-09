// Bluetooth LE. Дві незалежні речі на одній лампі:
//
// 1. Налаштування WiFi без підключення до точки доступу. Клієнт — web/setup.html у Chrome; спарювання не потрібне.
//   status (читання): стан лампи і мережі, оновлюється щосекунди
//   scan   (читання): {"seq", "list"} — результат останнього пошуку мереж
//   cmd    (запис):   {"cmd":"scan"} | {"cmd":"add","ssid":..,"pass":..} | {"cmd":"forget","ssid":..} | {"cmd":"forget"}
//
// 2. Пульт: лампа — Bluetooth-клавіатура з медіа-клавішами (HID over GATT). Її один раз спаровують у налаштуваннях
//    системи; далі дії сенсора шлють «пауза», «наступний трек», гучність або F13–F16.
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "app.h"

#define BLE_SERVICE "a9e10001-7c1e-4b6f-9d2a-41676e744c69"
#define BLE_STATUS  "a9e10002-7c1e-4b6f-9d2a-41676e744c69"
#define BLE_SCAN    "a9e10003-7c1e-4b6f-9d2a-41676e744c69"
#define BLE_CMD     "a9e10004-7c1e-4b6f-9d2a-41676e744c69"
const size_t VALUE_MAX = 500;          // стеля довжини значення характеристики — 512 байтів

static NimBLECharacteristic* statusChar = nullptr;
static NimBLECharacteristic* scanChar = nullptr;
static String        cmd;              // команда від сторінки; виконується в loop(), а не в потоці Bluetooth
static volatile bool cmdReady = false;
static uint32_t      scanSeq = 0;

// Саме std::string: з const char* бібліотека скопіювала б вказівник, а не текст
static void setJson(NimBLECharacteristic* c, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  c->setValue(std::string(out.c_str(), min((size_t)out.length(), VALUE_MAX)));
}

class CmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    if (cmdReady) return;
    cmd = c->getValue().c_str();
    cmdReady = true;
  }
};

// Опис звітів HID: звіт 1 — клавіатура (модифікатори + до шести клавіш), звіт 2 — одна медіа-клавіша
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,             // Generic Desktop, Keyboard, Application, Report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,   // 8 модифікаторів
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                          // зарезервований байт
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,   // 6 клавіш, коди до F24
  0xC0,
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,             // Consumer, Consumer Control, Application, Report ID 2
  0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,   // один 16-бітний код
  0xC0,
};
static NimBLECharacteristic* keyboardReport = nullptr;
static NimBLECharacteristic* mediaReport = nullptr;
static volatile uint8_t secured = 0;      // скільки підключених пристроїв пройшли спарювання: їм можна слати клавіші

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override { NimBLEDevice::startAdvertising(); }   // лишаємось видимими для другого пристрою
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
    if (desc->sec_state.encrypted && secured) secured--;
    NimBLEDevice::startAdvertising();
  }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (desc->sec_state.encrypted) secured++;
  }
};

bool remoteConnected() { return secured > 0; }
int  remoteBonds() { return NimBLEDevice::getNumBonds(); }
void remoteForget() { NimBLEDevice::deleteAllBonds(); }

// Натиснути й одразу відпустити: два звіти з паузою, інакше система може не помітити натискання
void remoteMedia(uint16_t usage) {
  if (!remoteConnected()) return;
  uint8_t down[2] = {(uint8_t)usage, (uint8_t)(usage >> 8)}, up[2] = {0, 0};
  mediaReport->setValue(down, sizeof down);
  mediaReport->notify();
  delay(20);
  mediaReport->setValue(up, sizeof up);
  mediaReport->notify();
}

void remoteKey(uint8_t code) {
  if (!remoteConnected()) return;
  uint8_t down[8] = {0, 0, code, 0, 0, 0, 0, 0}, up[8] = {};
  keyboardReport->setValue(down, sizeof down);
  keyboardReport->notify();
  delay(20);
  keyboardReport->setValue(up, sizeof up);
  keyboardReport->notify();
}

void bleBegin() {
  NimBLEDevice::init(apName.c_str());
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCallbacks());
  NimBLEService* svc = srv->createService(BLE_SERVICE);
  statusChar = svc->createCharacteristic(BLE_STATUS, NIMBLE_PROPERTY::READ);
  scanChar = svc->createCharacteristic(BLE_SCAN, NIMBLE_PROPERTY::READ);
  scanChar->setValue(std::string("{\"seq\":0,\"list\":[]}"));
  svc->createCharacteristic(BLE_CMD, NIMBLE_PROPERTY::WRITE)->setCallbacks(new CmdCallbacks());
  svc->start();

  // Пульт. Спарювання без коду («Just Works») із запам'ятовуванням: у лампи немає ні екрана, ні клавіш для коду
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEHIDDevice* hid = new NimBLEHIDDevice(srv);
  keyboardReport = hid->inputReport(1);
  mediaReport = hid->inputReport(2);
  hid->manufacturer("AgentLight");
  hid->pnp(0x02, 0xE502, 0xA111, 0x0100);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)REPORT_MAP, sizeof REPORT_MAP);
  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);          // клавіатура: так лампу показують налаштування системи
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->addServiceUUID(BLE_SERVICE);
  adv->setScanResponse(true);          // назва не влазить поруч із UUID сервісу, їде у відповіді на сканування
  adv->start();
}

static void scan() {
  Net found[20];
  size_t count = scanNets(found, 20);
  JsonDocument doc;
  doc["seq"] = ++scanSeq;
  JsonArray list = doc["list"].to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    list.add(found[i].ssid);
    if (measureJson(doc) > VALUE_MAX) { list.remove(list.size() - 1); break; }
  }
  setJson(scanChar, doc);
}

void pollBle() {
  static uint32_t statusAt = 0;
  if (millis() - statusAt > 1000) {
    statusAt = millis();
    // Після підключення пристрою оголошення зупиняється, і запустити його з обробника підключення не виходить.
    // Тому стежимо звідси: поки є вільне з'єднання, лампу має бути видно — інакше зі спарованим комп'ютером
    // ніхто інший не знайшов би її, щоб налаштувати WiFi.
    NimBLEServer* srv = NimBLEDevice::getServer();
    if (srv && srv->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS && !NimBLEDevice::getAdvertising()->isAdvertising())
      NimBLEDevice::startAdvertising();
    JsonDocument doc;
    fillWifi(doc.to<JsonObject>());
    doc["name"] = apName;
    doc["state"] = STATE_NAMES[aggregate()];
    setJson(statusChar, doc);
  }
  if (!cmdReady) return;
  JsonDocument in;
  DeserializationError bad = deserializeJson(in, cmd);
  cmdReady = false;
  if (bad) return;
  String name = in["cmd"] | "";
  const char* ssid = in["ssid"] | "";
  if (name == "scan") return scan();
  if (name == "add" && *ssid) addNet(ssid, in["pass"] | "");
  else if (name == "forget") forgetNet(ssid);
  else return;
  delay(500);                           // сторінка встигає отримати підтвердження запису
  ESP.restart();
}
