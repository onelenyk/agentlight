// Розбір події агента тим самим кодом, що в прошивці, але на комп'ютері: для тестів і для імітації лампи.
//   hookparse <агент> <стан> < подія.json   ->  один рядок JSON: {"skip":true} або {"state":...,"agent_id":...}
#include <iostream>
#include <iterator>
#include "../firmware/src/hook_parse.h"

static std::string quote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
    else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
    else out += (char)c;
  }
  return out + "\"";
}

int main(int argc, char** argv) {
  if (argc != 3) { std::cerr << "usage: hookparse <agent> <state> < event.json\n"; return 2; }
  std::string body((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  hookparse::Event ev = hookparse::parse(argv[1], argv[2], body);
  if (ev.skip) { std::cout << "{\"skip\":true}\n"; return 0; }
  std::cout << "{\"state\":" << quote(ev.state) << ",\"agent_id\":" << quote(ev.agentId) << ",\"name\":" << quote(ev.name)
            << ",\"message\":" << quote(ev.message) << ",\"task\":" << quote(ev.task) << "}\n";
}
