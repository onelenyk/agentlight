// Розбір події агента: з тіла хука дістає сесію, проєкт, запит і поточну дію та визначає стан лампи.
// Чистий C++ без Arduino — той самий код збирається в прошивку (hook.cpp) і в тести на комп'ютері (tests/).
// Тіло може бути обрізане посеред рядка (хук шле перші 4 КБ), тож поля шукаються текстом, а не розбором JSON.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace hookparse {
using std::string;

struct Event {
  bool   skip = false;                // подію треба проігнорувати
  string state, agentId, name, message, task;
};

// Позиція одразу після двокрапки ключа key, починаючи з from; -1, якщо ключа немає
inline int after(const string& body, const char* key, int from = 0) {
  string quoted = string("\"") + key + "\"";
  for (size_t found = body.find(quoted, from); found != string::npos; found = body.find(quoted, found + 1)) {
    size_t at = found + quoted.size();
    while (at < body.size() && isspace((unsigned char)body[at])) at++;
    if (at >= body.size() || body[at] != ':') continue;     // це було значення з таким текстом, а не ключ
    at++;
    while (at < body.size() && isspace((unsigned char)body[at])) at++;
    return (int)at;
  }
  return -1;
}

// Прибирає з кінця рядка обрізаний посередині символ UTF-8, щоб далі не пішов зіпсований текст
inline void dropPartialChar(string& text) {
  size_t i = text.size();
  while (i > 0 && ((unsigned char)text[i - 1] & 0xC0) == 0x80) i--;      // байти-продовження
  if (i == 0) { text.clear(); return; }
  unsigned char lead = text[i - 1];
  size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (text.size() - (i - 1) < need) text.resize(i - 1);
}

// Читає JSON-рядок, що починається в позиції at (на лапці); обрив тіла посеред рядка — не помилка
inline string readString(const string& body, int at, size_t max = 400) {
  string out;
  if (at < 0 || at >= (int)body.size() || body[at] != '"') return out;
  for (size_t i = at + 1; i < body.size() && out.size() < max; i++) {
    char c = body[i];
    if (c == '"') break;
    if (c != '\\') { out += c; continue; }
    if (++i >= body.size()) break;
    switch (body[i]) {
      case 'n': case 'r': case 't': out += ' '; break;
      case 'u': {                                  // \uXXXX -> UTF-8 (сурогатні пари пропускаємо)
        if (i + 4 >= body.size()) return out;
        unsigned u = strtoul(body.substr(i + 1, 4).c_str(), nullptr, 16);
        i += 4;
        if (u < 0x80) out += (char)u;
        else if (u < 0x800) { out += (char)(0xC0 | u >> 6); out += (char)(0x80 | (u & 0x3F)); }
        else if (u < 0xD800 || u > 0xDFFF) { out += (char)(0xE0 | u >> 12); out += (char)(0x80 | (u >> 6 & 0x3F)); out += (char)(0x80 | (u & 0x3F)); }
        break;
      }
      default: out += body[i];
    }
  }
  dropPartialChar(out);
  return out;
}

// Значення першого з перелічених ключів, яке є рядком (або першим рядком у масиві)
inline string field(const string& body, std::initializer_list<const char*> keys, int from = 0) {
  for (const char* key : keys) {
    int at = after(body, key, from);
    if (at < 0) continue;
    if (body[at] == '[') { at++; while (at < (int)body.size() && isspace((unsigned char)body[at])) at++; }
    string value = readString(body, at);
    if (!value.empty()) return value;
  }
  return string();
}

inline string baseName(string path) {
  while (!path.empty() && path.back() == '/') path.pop_back();
  size_t slash = path.rfind('/');
  return slash == string::npos ? path : path.substr(slash + 1);
}

inline bool oneOf(const string& value, std::initializer_list<const char*> list) {
  for (const char* item : list) if (value == item) return true;
  return false;
}

// tool — агент з адреси /hook/<агент>/<стан>; state — стан або псевдостан:
//   stop         — кінець ходу з причиною: Cursor (status), Antigravity (terminationReason, fullyIdle)
//   notification — сповіщення: «чекає» лише для запиту дозволу чи питання, решта ігнорується
inline Event parse(const string& tool, string state, const string& body) {
  Event ev;
  string sid = field(body, {"session_id", "sessionId", "conversationId", "conversation_id", "trajectory_id"});
  string event = field(body, {"hook_event_name", "hookEventName", "event"});
  string why = field(body, {"status", "terminationReason"});
  string kind = field(body, {"notification_type", "notificationType"});
  int errAt = after(body, "error");
  string err = errAt < 0 ? string() : body[errAt] == '{' ? field(body, {"message"}, errAt) : readString(body, errAt);

  // Що саме робить інструмент: назва і найкорисніший з аргументів
  int callAt = after(body, "toolCall");
  int inputAt = std::max(std::max(after(body, "tool_input"), after(body, "toolArgs")), callAt);
  string toolName = field(body, {"tool_name", "toolName"});
  if (toolName.empty() && callAt >= 0) toolName = field(body, {"name"}, callAt);
  string what;
  if (inputAt >= 0) {
    what = field(body, {"description"}, inputAt);
    if (what.empty()) what = baseName(field(body, {"file_path"}, inputAt));
    if (what.empty()) what = field(body, {"pattern", "command"}, inputAt);
  }
  string toolText = !toolName.empty() && !what.empty() ? toolName + ": " + what : toolName + what;

  if (state == "stop") {
    int idleAt = after(body, "fullyIdle");
    if (why == "aborted") state = "idle";                                        // Cursor: користувач перервав
    else if (why == "error" || !err.empty() || why == "max_steps_exceeded") state = "error";
    else if (idleAt >= 0 && body.compare(idleAt, 5, "false") == 0) state = "busy";   // Antigravity: фонові задачі ще йдуть
    else state = "done";
  } else if (state == "notification") {
    if (!oneOf(kind, {"permission_prompt", "elicitation_dialog", "elicitation_url_dialog", "agent_needs_input", "ToolPermission"})) {
      ev.skip = true;
      return ev;
    }
    state = "waiting";
  }
  // OpenCode з oh-my-openagent запускає хуки Claude Code зі своїм session_id (ses_...), але без Stop:
  // лампа зависла б у «працює». Таку сесію веде плагін OpenCode, тут її пропускаємо.
  if (tool == "claude" && sid.compare(0, 4, "ses_") == 0) {
    ev.skip = true;
    return ev;
  }

  if (state == "error") ev.message = "Помилка: " + (!err.empty() ? err : !why.empty() ? why : string("невідома"));
  else if (oneOf(event, {"UserPromptSubmit", "userPromptSubmitted", "beforeSubmitPrompt", "BeforeAgent", "PreInvocation"})) ev.message = "думає";
  else if (event == "PreToolUse" && state == "waiting") ev.message = field(body, {"question"}, std::max(inputAt, 0));   // AskUserQuestion
  else if (event == "Notification" || event == "notification" || event == "Elicitation") ev.message = field(body, {"message"});
  else if (event == "PreCompact" || event == "preCompact") ev.message = "стискає контекст";
  else if (state == "waiting" && !toolText.empty()) ev.message = "Дозвіл — " + toolText;
  else if (state == "busy") ev.message = toolText;
  if (state == "waiting" && ev.message.empty()) ev.message = toolText;

  ev.state = state;
  ev.agentId = tool + "-" + (sid.empty() ? string("x") : sid.substr(0, 8));
  ev.name = baseName(field(body, {"cwd", "workspacePaths", "workspace_roots", "working_dir"}));
  if (ev.name.empty()) ev.name = tool;          // без відкритої теки показуємо хоча б, який це агент
  ev.task = field(body, {"prompt"});
  return ev;
}
}  // namespace hookparse
