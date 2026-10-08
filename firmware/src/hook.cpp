// Прийом хуків від агентів: POST /hook/<інструмент>/<стан>, у тілі — JSON події як є (хук шле перші 4 КБ).
// Лампа сама дістає з нього сесію, проєкт, запит і поточну дію, тому на комп'ютері не потрібні ні скрипти, ні jq:
// хук — це один виклик curl. Тіло може бути обрізане посеред рядка, тож поля шукаються текстом, а не розбором JSON.
//
// <стан>: busy | waiting | done | error | idle, або псевдостан, який залежить від вмісту події:
//   stop         — кінець ходу з причиною: Cursor (status), Antigravity (terminationReason, fullyIdle)
//   notification — сповіщення: «чекає» лише для запиту дозволу чи питання, решта ігнорується
#include <uri/UriBraces.h>
#include "app.h"

// Позиція одразу після двокрапки ключа key, починаючи з from; -1, якщо ключа немає
static int after(const String& body, const char* key, int from = 0) {
  String quoted = String('"') + key + '"';
  int at = body.indexOf(quoted, from);
  if (at < 0) return -1;
  at += quoted.length();
  while (at < (int)body.length() && isspace(body[at])) at++;
  if (at >= (int)body.length() || body[at] != ':') return -1;
  at++;
  while (at < (int)body.length() && isspace(body[at])) at++;
  return at;
}

// Читає JSON-рядок, що починається в позиції at (на лапці); обрив тіла посеред рядка — не помилка
static String readString(const String& body, int at, size_t max = 400) {
  String out;
  if (at < 0 || at >= (int)body.length() || body[at] != '"') return out;
  for (int i = at + 1; i < (int)body.length() && out.length() < max; i++) {
    char c = body[i];
    if (c == '"') break;
    if (c != '\\') { out += c; continue; }
    if (++i >= (int)body.length()) break;
    switch (body[i]) {
      case 'n': case 'r': case 't': out += ' '; break;
      case 'u': {                                  // \uXXXX -> UTF-8 (сурогатні пари пропускаємо)
        if (i + 4 >= (int)body.length()) return out;
        uint16_t u = strtoul(body.substring(i + 1, i + 5).c_str(), nullptr, 16);
        i += 4;
        if (u < 0x80) out += (char)u;
        else if (u < 0x800) { out += (char)(0xC0 | u >> 6); out += (char)(0x80 | (u & 0x3F)); }
        else if (u < 0xD800 || u > 0xDFFF) { out += (char)(0xE0 | u >> 12); out += (char)(0x80 | (u >> 6 & 0x3F)); out += (char)(0x80 | (u & 0x3F)); }
        break;
      }
      default: out += body[i];
    }
  }
  return out;
}

// Значення першого з перелічених ключів, яке є рядком (або першим рядком у масиві)
static String field(const String& body, std::initializer_list<const char*> keys, int from = 0) {
  for (const char* key : keys) {
    int at = after(body, key, from);
    if (at < 0) continue;
    if (body[at] == '[') { at++; while (at < (int)body.length() && isspace(body[at])) at++; }
    String value = readString(body, at);
    if (value.length()) return value;
  }
  return String();
}

static String baseName(String path) {
  while (path.endsWith("/")) path.remove(path.length() - 1);
  return path.substring(path.lastIndexOf('/') + 1);
}

static bool oneOf(const String& value, std::initializer_list<const char*> list) {
  for (const char* item : list) if (value == item) return true;
  return false;
}

static void handleHook() {
  String tool = server.pathArg(0), state = server.pathArg(1);
  const String& body = server.arg("plain");
  server.send(200, "application/json", "{}");     // відповідаємо одразу: хук не має чекати на розбір

  String sid = field(body, {"session_id", "sessionId", "conversationId", "conversation_id", "trajectory_id"});
  String event = field(body, {"hook_event_name", "hookEventName", "event"});
  String why = field(body, {"status", "terminationReason"});
  String kind = field(body, {"notification_type", "notificationType"});
  String prompt = field(body, {"prompt"});
  int errAt = after(body, "error");
  String err = errAt < 0 ? String() : body[errAt] == '{' ? field(body, {"message"}, errAt) : readString(body, errAt);

  // Що саме робить інструмент: назва і найкорисніший з аргументів
  int inputAt = max(max(after(body, "tool_input"), after(body, "toolArgs")), after(body, "toolCall"));
  String toolName = field(body, {"tool_name", "toolName"});
  if (!toolName.length() && after(body, "toolCall") >= 0) toolName = field(body, {"name"}, after(body, "toolCall"));
  String what;
  if (inputAt >= 0) {
    what = field(body, {"description"}, inputAt);
    if (!what.length()) what = baseName(field(body, {"file_path"}, inputAt));
    if (!what.length()) what = field(body, {"pattern", "command"}, inputAt);
  }
  String toolText = toolName.length() && what.length() ? toolName + ": " + what : toolName + what;

  if (state == "stop") {
    int idleAt = after(body, "fullyIdle");
    if (why == "aborted") state = "idle";                                        // Cursor: користувач перервав
    else if (why == "error" || err.length() || why == "max_steps_exceeded") state = "error";
    else if (idleAt >= 0 && body.startsWith("false", idleAt)) state = "busy";    // Antigravity: фонові задачі ще йдуть
    else state = "done";
  } else if (state == "notification") {
    if (!oneOf(kind, {"permission_prompt", "elicitation_dialog", "elicitation_url_dialog", "agent_needs_input", "ToolPermission"})) return;
    state = "waiting";
  }
  // OpenCode з oh-my-openagent запускає хуки Claude Code зі своїм session_id (ses_...), але без Stop:
  // лампа зависла б у «працює». Таку сесію веде плагін OpenCode, тут її пропускаємо.
  if (tool == "claude" && sid.startsWith("ses_")) return;

  String message;
  if (state == "error") message = "Помилка: " + (err.length() ? err : why.length() ? why : String("невідома"));
  else if (oneOf(event, {"UserPromptSubmit", "userPromptSubmitted", "beforeSubmitPrompt", "BeforeAgent", "PreInvocation"})) message = "думає";
  else if (event == "PreToolUse" && state == "waiting") message = field(body, {"question"}, max(inputAt, 0));   // AskUserQuestion
  else if (event == "Notification" || event == "notification" || event == "Elicitation") message = field(body, {"message"});
  else if (event == "PreCompact" || event == "preCompact") message = "стискає контекст";
  else if (state == "waiting" && toolText.length()) message = "Дозвіл — " + toolText;
  else if (state == "busy") message = toolText;
  if (state == "waiting" && !message.length()) message = toolText;

  JsonDocument in;
  in["state"] = state;
  in["agent_id"] = tool + "-" + (sid.length() ? sid.substring(0, 8) : String("x"));
  String name = baseName(field(body, {"cwd", "workspacePaths", "workspace_roots", "working_dir"}));
  in["name"] = name.length() ? name : tool;        // без відкритої теки показуємо хоча б, який це агент
  in["message"] = message;
  if (prompt.length()) in["task"] = prompt;
  applyStatus(in);
}

void hookBegin() {
  server.on(UriBraces("/hook/{}/{}"), HTTP_POST, handleHook);
}
