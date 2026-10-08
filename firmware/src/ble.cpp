// Bluetooth LE: налаштування WiFi без підключення до точки доступу. Клієнт — web/setup.html у Chrome.
//   status (читання): стан лампи і мережі, оновлюється щосекунди
//   scan   (читання): {"seq", "list"} — результат останнього пошуку мереж
//   cmd    (запис):   {"cmd":"scan"} | {"cmd":"add","ssid":..,"pass":..} | {"cmd":"forget","ssid":..} | {"cmd":"forget"}
#include <NimBLEDevice.h>
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

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer*) override { NimBLEDevice::startAdvertising(); }
};

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
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
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
