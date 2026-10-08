// WiFi: до MAX_NETS збережених мереж; якщо жодної поруч немає — точка доступу AgentLight-XXXX з порталом.
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "app.h"

const uint8_t  MAX_NETS = 5;
const uint32_t CONNECT_TIMEOUT_MS = 20000;
const uint32_t PORTAL_RETRY_MS = 180000;   // у порталі зі збереженими мережами: перезапуск і нова спроба, якщо ніхто не підключений

String apName;
bool   portal = false;
static JsonDocument nets;                  // [{"s": назва, "p": пароль}], остання додана — перша
static DNSServer    dns;

static void saveNets() {
  String out;
  serializeJson(nets, out);
  prefs.putString("nets", out);
}

void forgetNet(const char* ssid) {
  JsonArray list = nets.as<JsonArray>();
  for (size_t i = list.size(); i-- > 0;)
    if (!*ssid || !strcmp(list[i]["s"] | "", ssid)) list.remove(i);
  saveNets();
}

// Додає мережу до збережених або оновлює її пароль; остання додана стає першою
void addNet(const char* ssid, const char* pass) {
  JsonDocument next;
  JsonObject first = next.add<JsonObject>();
  first["s"] = ssid;
  first["p"] = pass;
  for (JsonObject n : nets.as<JsonArray>())
    if (next.size() < MAX_NETS && strcmp(n["s"] | "", ssid)) next.add(n);
  nets = next;
  saveNets();
}

size_t scanNets(Net* out, size_t max) {
  size_t count = 0;
  int found = WiFi.scanNetworks();          // відсортовано за силою сигналу
  for (int i = 0; i < found && count < max; i++) {
    String ssid = WiFi.SSID(i);
    bool seen = ssid.isEmpty();
    for (size_t k = 0; k < count; k++) seen |= out[k].ssid == ssid;
    if (!seen) out[count++] = {ssid, (int)WiFi.RSSI(i)};
  }
  WiFi.scanDelete();
  return count;
}

void fillWifi(JsonObject w) {
  w["portal"] = portal;
  w["ap"] = apName;
  w["ssid"] = portal ? String() : WiFi.SSID();
  JsonArray saved = w["saved"].to<JsonArray>();
  for (JsonObject n : nets.as<JsonArray>()) saved.add(n["s"]);
  w["ip"] = (portal ? WiFi.softAPIP() : WiFi.localIP()).toString();
  w["rssi"] = WiFi.RSSI();
  w["host"] = HOSTNAME ".local";
}

// Дія сенсора «запит на адресу»: POST із JSON про те, що сталось. Лише http — на TLS у прошивці немає місця.
void fireWebhook(const char* gesture) {
  if (portal || !webhookUrl.startsWith("http://")) return;
  String rest = webhookUrl.substring(7);
  int slash = rest.indexOf('/');
  String host = slash < 0 ? rest : rest.substring(0, slash), path = slash < 0 ? "/" : rest.substring(slash);
  uint16_t port = 80;
  int colon = host.indexOf(':');
  if (colon >= 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
  JsonDocument doc;
  doc["device"] = apName;
  doc["gesture"] = gesture;
  doc["mode"] = MODE_NAMES[cfg.mode];
  doc["state"] = STATE_NAMES[aggregate()];
  String body;
  serializeJson(doc, body);
  WiFiClient client;
  if (!client.connect(host.c_str(), port, 1500)) return;
  client.printf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                path.c_str(), host.c_str(), (unsigned)body.length());
  client.print(body);
  client.flush();
  client.stop();                             // відповідь не читаємо: лампа не має зависати на чужому сервері
}

static bool tryNet(const char* ssid, const char* pass) {
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
static bool connectSaved() {
  JsonArray list = nets.as<JsonArray>();
  if (!list.size()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  Net   seen[20];
  size_t count = scanNets(seen, 20);
  for (size_t i = 0; i < count; i++)        // seen уже за спаданням сигналу: перша збережена — найсильніша
    for (JsonObject n : list)
      if (seen[i].ssid == (n["s"] | "")) return tryNet(n["s"], n["p"]);
  Serial.println("WiFi: жодної збереженої мережі поруч немає");
  // прихована мережа у скануванні не видна: якщо збережена лише одна, пробуємо її наосліп
  return list.size() == 1 && tryNet(list[0]["s"], list[0]["p"]);
}

static void startPortal() {
  portal = true;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);  // STA лишається для сканування мереж
  WiFi.softAP(apName.c_str());
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  dns.start(53, "*", WiFi.softAPIP());  // будь-який домен веде на нас: телефон сам відкриє сторінку
  Serial.printf("Портал: точка доступу %s, http://%s/\n", apName.c_str(), WiFi.softAPIP().toString().c_str());
}

void netBegin() {
  if (deserializeJson(nets, prefs.isKey("nets") ? prefs.getString("nets") : String("[]")) || !nets.is<JsonArray>()) nets.to<JsonArray>();
  if (prefs.isKey("ssid")) {   // перехід зі старої прошивки, де мережа була одна
    addNet(prefs.getString("ssid", "").c_str(), prefs.getString("pass", "").c_str());
    prefs.remove("ssid");
    prefs.remove("pass");
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof suffix, "%02X%02X", mac[4], mac[5]);
  apName = String("AgentLight-") + suffix;

  if (connectSaved()) {
    Serial.printf("WiFi: готово, http://%s/ або http://" HOSTNAME ".local/\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  } else {
    startPortal();
  }
}

void pollNet() {
  if (!portal) return;
  dns.processNextRequest();
  if (nets.size() && millis() > PORTAL_RETRY_MS && WiFi.softAPgetStationNum() == 0) ESP.restart();
}
