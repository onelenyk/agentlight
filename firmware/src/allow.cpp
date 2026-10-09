// «Дозволити» (експериментально): агент просить дозвіл — власник торкається лампи замість клавіатури.
// Дотик — дозволити один раз, утримання — відхилити; без відповіді за ASK_MS агент показує звичайне питання.
//
// Запобіжники:
//   - вимкнено, доки не ввімкнуть на сторінці лампи;
//   - комп'ютер має бути спарований: при встановленні він надсилає свій секрет, і лампа приймає його лише після дотику;
//   - відповідь лампи підписана цим секретом (HMAC-SHA256 від «nonce:рішення»), тож інший пристрій у мережі
//     не може видати себе за лампу й «дозволити» замість неї;
//   - рішення приймає тільки дотик до сенсора: запиту, який каже лампі «дозволь», не існує;
//   - небезпечні команди й вимкнені групи інструментів лампа не пропонує взагалі (allow_rules.h);
//   - одночасно лише один запит: другий скасовує обидва, і кожен агент питає в терміналі.
//
//   POST /api/allow/pair   {"key","secret","name"}            -> лампа блимає й чекає дотику
//   GET  /api/allow/pair?key=K                                  -> {"state":"pending|accepted|rejected|expired"}
//   POST /api/allow/ask?agent=A&key=K&nonce=N  (тіло — подія хука як є)
//        або JSON {"key","nonce","agent_id","name","tool","detail"}  -> {"id":N} або {"refused":"причина"}
//   GET  /api/allow/ask?id=N                                    -> {"state":"pending|allow|deny|expired","sig":...}
//   POST /api/allow/cancel {"id":N}                             -> на питання вже відповіли в терміналі
//   POST /api/allow/forget                                      -> забути всі спаровані комп'ютери
#include <mbedtls/md.h>
#include "allow_rules.h"
#include "app.h"
#include "hook_parse.h"

const uint32_t ASK_MS = 15000, PAIR_MS = 30000, KEEP_MS = 10000;   // KEEP_MS: скільки відповідь чекає, щоб її забрали
const uint8_t  MAX_KEYS = 4;

enum Decision : uint8_t { D_PENDING, D_ALLOW, D_DENY, D_EXPIRED };   // з префіксом: назву PENDING зайняв макрос ядра
static const char* const DECISIONS[] = {"pending", "allow", "deny", "expired"};

static struct {
  bool     active = false;        // запит ще показується на лампі
  uint32_t id = 0, at = 0, decidedAt = 0;
  uint8_t  decision = D_EXPIRED;
  String   secret, nonce, agent, name, tool, detail;
} ask;

static struct {
  bool     active = false;
  uint32_t at = 0;
  uint8_t  decision = D_EXPIRED;    // D_ALLOW тут означає «спарювання прийнято»
  String   key, secret, name;
} pairing;

static JsonDocument keys;         // [{"k": ідентифікатор, "s": секрет, "n": назва комп'ютера}]

static void reply(int code, JsonDocument& doc) { sendJson(code, doc); }
static void refuse(const char* why) { JsonDocument d; d["refused"] = why; reply(200, d); }

static String secretFor(const String& key) {
  for (JsonObject k : keys.as<JsonArray>()) if (key == (k["k"] | "")) return k["s"] | "";
  return String();
}

static String sign(const String& secret, const String& text) {
  uint8_t mac[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t*)secret.c_str(), secret.length(),
                  (const uint8_t*)text.c_str(), text.length(), mac);
  char hex[65];
  for (uint8_t i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", mac[i]);
  return hex;
}

bool allowAsking() { return ask.active; }
bool allowPairing() { return pairing.active; }

// Дотик під час запиту чи спарювання: yes — дотик, no — утримання
void allowTouch(bool yes) {
  if (pairing.active) {
    pairing.active = false;
    pairing.decision = yes ? D_ALLOW : D_DENY;
    if (yes) {
      JsonDocument next;                                   // новий ключ перший; той самий комп'ютер замінює свій старий
      JsonObject first = next.add<JsonObject>();
      first["k"] = pairing.key; first["s"] = pairing.secret; first["n"] = pairing.name;
      for (JsonObject k : keys.as<JsonArray>())
        if (next.size() < MAX_KEYS && pairing.key != (k["k"] | "")) next.add(k);
      keys = next;
      String out;
      serializeJson(keys, out);
      prefs.putString("allowkeys", out);
    }
  } else if (ask.active) {
    ask.active = false;
    ask.decision = yes ? D_ALLOW : D_DENY;
    ask.decidedAt = millis();
  }
}

void pollAllow() {
  if (ask.active && millis() - ask.at > ASK_MS) { ask.active = false; ask.decision = D_EXPIRED; ask.decidedAt = millis(); }
  if (pairing.active && millis() - pairing.at > PAIR_MS) { pairing.active = false; pairing.decision = D_EXPIRED; }
}

void fillAllow(JsonObject o) {
  o["enabled"] = (bool)cfg.allowEnabled;
  o["edits"] = (bool)(cfg.allowCats & allowrules::EDIT);
  o["commands"] = (bool)(cfg.allowCats & allowrules::COMMAND);
  o["other"] = (bool)(cfg.allowCats & allowrules::OTHER);
  JsonArray names = o["computers"].to<JsonArray>();
  for (JsonObject k : keys.as<JsonArray>()) names.add(k["n"]);
  o["pairing"] = pairing.active ? pairing.name : String();
  if (!ask.active) return;
  JsonObject p = o["asking"].to<JsonObject>();
  p["agent"] = ask.agent;
  p["name"] = ask.name;
  p["tool"] = ask.tool;
  p["detail"] = ask.detail;
  p["left"] = (ASK_MS - (millis() - ask.at)) / 1000;
}

static void handlePairStart() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  String key = in["key"] | "", secret = in["secret"] | "";
  if (key.length() < 8 || secret.length() < 32) return refuse("bad key");
  if (ask.active || pairing.active) return refuse("busy");
  pairing.active = true;
  pairing.at = millis();
  pairing.decision = D_PENDING;
  pairing.key = key;
  pairing.secret = secret;
  pairing.name = String(in["name"] | "комп'ютер").substring(0, 40);
  JsonDocument out;
  out["pairing"] = true;
  reply(200, out);
}

static void handlePairState() {
  JsonDocument out;
  bool mine = server.arg("key") == pairing.key;
  out["state"] = !mine ? "expired" : pairing.decision == D_ALLOW ? "accepted" : pairing.decision == D_DENY ? "rejected" : DECISIONS[pairing.decision];
  reply(200, out);
}

static void handleAsk() {
  String body = server.arg("plain"), key = server.arg("key"), nonce = server.arg("nonce");
  String agent, name, tool, detail;
  if (server.hasArg("agent")) {                             // подія хука як є: розбираємо тим самим кодом, що й стани
    if (body.length() >= 3900) return refuse("too long");   // обрізану команду не перевірити
    hookparse::Event ev = hookparse::parse(server.arg("agent").c_str(), "waiting", body.c_str());
    agent = ev.agentId.c_str(); name = ev.name.c_str(); tool = ev.tool.c_str(); detail = ev.command.c_str();
  } else {
    JsonDocument in;
    if (deserializeJson(in, body)) return refuse("bad json");
    key = in["key"] | ""; nonce = in["nonce"] | "";
    agent = in["agent_id"] | ""; name = in["name"] | ""; tool = in["tool"] | ""; detail = in["detail"] | "";
  }
  String secret = secretFor(key);
  if (!cfg.allowEnabled) return refuse("disabled");
  if (!secret.length() || nonce.length() < 8) return refuse("not paired");
  if (!tool.length()) return refuse("unknown tool");
  if (!(cfg.allowCats & allowrules::category(tool.c_str()))) return refuse("category");
  if (allowrules::dangerous(detail.c_str())) return refuse("dangerous");
  if (pairing.active) return refuse("busy");
  if (ask.active) {                                         // двоє одразу: лампа не вгадуватиме, кому відповідь
    ask.active = false;
    ask.decision = D_EXPIRED;
    ask.decidedAt = millis();
    return refuse("busy");
  }
  ask.active = true;
  ask.id++;
  ask.at = millis();
  ask.decision = D_PENDING;
  ask.secret = secret; ask.nonce = nonce; ask.agent = agent; ask.name = name; ask.tool = tool;
  ask.detail = detail.substring(0, 200);
  lightSwitch(true);                                        // вимкнене світло не має ховати запит
  JsonDocument out;
  out["id"] = ask.id;
  reply(200, out);
}

static void handleAskState() {
  JsonDocument out;
  uint32_t id = server.arg("id").toInt();
  bool known = id == ask.id && (ask.active || millis() - ask.decidedAt < KEEP_MS);
  uint8_t decision = known ? ask.decision : D_EXPIRED;
  out["state"] = DECISIONS[decision];
  if (decision == D_ALLOW || decision == D_DENY) out["sig"] = sign(ask.secret, ask.nonce + ":" + DECISIONS[decision]);
  reply(200, out);
}

static void handleCancel() {
  JsonDocument in, out;
  deserializeJson(in, server.arg("plain"));
  if (ask.active && (in["id"] | 0u) == ask.id) { ask.active = false; ask.decision = D_EXPIRED; ask.decidedAt = millis(); }
  out["ok"] = true;
  reply(200, out);
}

static void handleForget() {
  keys.to<JsonArray>();
  prefs.remove("allowkeys");
  JsonDocument out;
  out["ok"] = true;
  reply(200, out);
}

void allowBegin() {
  if (deserializeJson(keys, prefs.isKey("allowkeys") ? prefs.getString("allowkeys") : String("[]")) || !keys.is<JsonArray>())
    keys.to<JsonArray>();
  server.on("/api/allow/pair", HTTP_POST, handlePairStart);
  server.on("/api/allow/pair", HTTP_GET, handlePairState);
  server.on("/api/allow/ask", HTTP_POST, handleAsk);
  server.on("/api/allow/ask", HTTP_GET, handleAskState);
  server.on("/api/allow/cancel", HTTP_POST, handleCancel);
  server.on("/api/allow/forget", HTTP_POST, handleForget);
#ifdef ALLOW_TEST_TOUCH
  // Лише для перевірки на столі: дотик запитом. У звичайних збірках цього маршруту немає —
  // інакше будь-хто в мережі міг би «торкнутись» лампи замість власника.
  server.on("/api/allow/touch", HTTP_POST, [] {
    allowTouch(server.arg("yes") == "1");
    server.send(200, "application/json", "{}");
  });
#endif
}
