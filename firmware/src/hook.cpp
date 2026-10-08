// Прийом хуків від агентів: POST /hook/<агент>/<стан>, у тілі — JSON події як є (хук шле перші 4 КБ).
// Лампа сама дістає з нього сесію, проєкт, запит і поточну дію, тому на комп'ютері не потрібні ні скрипти, ні jq:
// хук — це один виклик curl. Сам розбір — у hook_parse.h, він же перевіряється тестами в tests/.
#include <uri/UriBraces.h>
#include "app.h"
#include "hook_parse.h"

static void handleHook() {
  hookparse::Event ev = hookparse::parse(server.pathArg(0).c_str(), server.pathArg(1).c_str(), server.arg("plain").c_str());
  server.send(200, "application/json", "{}");
  if (ev.skip) return;
  JsonDocument in;
  in["state"] = ev.state;
  in["agent_id"] = ev.agentId;
  in["name"] = ev.name;
  in["message"] = ev.message;
  if (!ev.task.empty()) in["task"] = ev.task;
  applyStatus(in);
}

void hookBegin() {
  server.on(UriBraces("/hook/{}/{}"), HTTP_POST, handleHook);
}
