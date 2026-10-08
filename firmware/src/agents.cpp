// Список агентів: кожен agent_id має свій стан, лампа показує найважливіший.
#include "app.h"

struct Agent {
  bool     used = false;
  String   id, name, task, message;  // name — проєкт, task — останній запит юзера, message — що робить зараз
  State    state = ST_IDLE;
  uint32_t at = 0;       // останнє оновлення (для TTL)
  uint32_t since = 0;    // відколи в цьому стані
};
const uint8_t MAX_AGENTS = 8;
static Agent agents[MAX_AGENTS];

static int8_t stateFromName(const char* name) {
  for (int8_t i = 0; i < ST_COUNT; i++)
    if (!strcmp(name, STATE_NAMES[i])) return i;
  return -1;
}

// Обрізає рядок до max байтів, не розрізаючи символ UTF-8
static String clip(const char* text, size_t max) {
  if (strlen(text) <= max) return text;
  while (max > 0 && ((uint8_t)text[max] & 0xC0) == 0x80) max--;
  String out;
  out.concat(text, max);
  return out + "…";
}

// Знаходить або створює запис агента і ставить йому стан; для idle запис видаляється (повертає nullptr)
static Agent* upsert(const String& id, State state) {
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

void fillAgents(JsonArray list) {
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
}

void clearAgents(bool keepBusy) {
  for (auto& a : agents)
    if (!(keepBusy && a.state == ST_BUSY)) a.used = false;
}
