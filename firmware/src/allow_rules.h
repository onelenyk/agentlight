// Правила функції «Дозволити»: що можна схвалювати дотиком до лампи, а що — лише в терміналі.
// Чистий C++ без Arduino — у прошивці (allow.cpp) і в тестах на комп'ютері.
#pragma once
#include <cctype>
#include <initializer_list>
#include <string>

namespace allowrules {
using std::string;

enum Category : uint8_t { EDIT = 1, COMMAND = 2, OTHER = 4 };

inline string lower(const string& text) {
  string out = text;
  for (char& c : out) c = (char)tolower((unsigned char)c);
  return out;
}

inline bool has(const string& text, std::initializer_list<const char*> parts) {   // усі частини є в тексті
  for (const char* part : parts) if (text.find(part) == string::npos) return false;
  return true;
}

// До якої групи належить інструмент: редагування файлів, команда оболонки чи все інше (мережа, MCP...)
inline Category category(const string& tool) {
  string t = lower(tool);
  for (const char* name : {"edit", "write", "patch", "notebook"}) if (t.find(name) != string::npos) return EDIT;
  for (const char* name : {"bash", "shell", "command", "exec"}) if (t.find(name) != string::npos) return COMMAND;
  return OTHER;
}

// Команди, які з лампи схвалювати не можна за жодних налаштувань: помилку тут не виправити.
// Список навмисно широкий: хибна відмова коштує одного натискання в терміналі.
inline bool dangerous(const string& command) {
  string c = " " + lower(command) + " ";
  for (const char* part : {"sudo ", "rm -r", "rm -f", " -rf", " -fr", "mkfs", "dd if=", "dd of=", ":(){", "shutdown", "reboot",
                           "diskutil", "format ", "chmod -r", "chown -r", "> /dev/", "killall", "pkill", "launchctl",
                           "defaults write", "crontab", "drop table", "drop database", "truncate ", "delete from",
                           "npm publish", "cargo publish", "gem push", "twine upload", "docker system prune", "docker rm",
                           "kubectl delete", "terraform destroy", "terraform apply", "git reset --hard", "git clean",
                           "git checkout -- ", "git branch -d", "gh repo delete", "ssh ", "scp ", "eval ", "base64 -d",
                           "/etc/", "~/.ssh", ".env", "passwd", "keychain", "security "})
    if (c.find(part) != string::npos) return true;
  if (has(c, {"git push", "--force"}) || has(c, {"git push", " -f"}) || has(c, {"git push", " +"})) return true;
  for (const char* fetch : {"curl", "wget"})                                    // завантажити й одразу виконати
    for (const char* shell : {"| sh", "|sh", "| bash", "|bash", "| zsh", "|zsh", "| python", "|python", "| node", "|node"})
      if (has(c, {fetch, shell})) return true;
  return false;
}
}  // namespace allowrules
