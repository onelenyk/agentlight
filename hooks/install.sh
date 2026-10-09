# Підключає агента до лампи AgentLight. Запуск (лампа сама роздає цей файл і підставляє свою адресу в LAMP):
#   curl -4 -fsS http://agentlight.local/install.sh | sh -s -- <агент>
# <агент>: claude | opencode | codex | gemini | antigravity | copilot | qwen | cursor
# Додаткове слово allow (лише claude й opencode) вмикає експериментальне «Дозволити»: дозвіл агентові дотиком до лампи.
#   curl -4 -fsS http://agentlight.local/install.sh | sh -s -- claude allow
# Тоді скрипт ще спаровує цей комп'ютер із лампою — попросить торкнутись її — і потребує openssl.
# Скрипт завантажує з лампи готові хуки й дописує їх до налаштувань агента, не чіпаючи чужих; стару версію
# файлу зберігає поруч як *.bak-agentlight-<час>. Для дописування потрібен python3 або node.
set -e
LAMP="${LAMP:-http://agentlight.local}"
TOOL="$1"
case "$TOOL" in
  claude)      FILE="$HOME/.claude/settings.json";       MODE=hooks ;;
  codex)       FILE="$HOME/.codex/hooks.json";           MODE=hooks ;;
  gemini)      FILE="$HOME/.gemini/settings.json";       MODE=hooks ;;
  qwen)        FILE="$HOME/.qwen/settings.json";         MODE=hooks ;;
  cursor)      FILE="$HOME/.cursor/hooks.json";          MODE=hooks ;;
  antigravity) FILE="$HOME/.gemini/config/hooks.json";   MODE=key ;;
  copilot)     FILE="$HOME/.copilot/hooks/agentlight.json";            MODE=file ;;
  opencode)    FILE="$HOME/.config/opencode/plugins/agentlight.js";    MODE=file ;;
  *) echo "Вкажи агента: claude, opencode, codex, gemini, antigravity, copilot, qwen або cursor." >&2; exit 1 ;;
esac
[ "$TOOL" = opencode ] && SRC="$LAMP/setup/opencode.js" || SRC="$LAMP/setup/$TOOL.json"

if [ "$2" = allow ]; then
  case "$TOOL" in claude|opencode) ;; *) echo "«Дозволити» є лише для claude і opencode." >&2; exit 1 ;; esac
  command -v openssl >/dev/null 2>&1 || { echo "Для «Дозволити» потрібен openssl." >&2; exit 1; }
  DIR="$HOME/.agentlight"
  mkdir -p "$DIR"
  # Секрет цього комп'ютера: ним лампа підписує свої відповіді, тож ніхто інший у мережі не «дозволить» замість неї.
  # Лампа приймає його лише після дотику власника.
  KEY=$(openssl rand -hex 4)
  SECRET=$(openssl rand -hex 16)
  NAME=$(hostname | tr -cd 'A-Za-z0-9._-' | cut -c1-40)
  curl -4 -fsS -m 5 -X POST -H 'Content-Type: application/json' \
    -d "{\"key\":\"$KEY\",\"secret\":\"$SECRET\",\"name\":\"$NAME\"}" "$LAMP/api/allow/pair" | grep -q '"pairing": *true' \
    || { echo "Лампа не почала спарювання: вона зайнята іншим запитом або прошивка застара." >&2; exit 1; }
  echo "Лампа блимає блакитним: торкнись її, щоб підтвердити спарювання з цим комп'ютером (є 30 секунд)…"
  STATE=pending
  for _ in $(seq 1 36); do
    sleep 1
    STATE=$(curl -4 -fsS -m 3 "$LAMP/api/allow/pair?key=$KEY" | sed -n 's/.*"state": *"\([a-z]*\)".*/\1/p')
    [ "$STATE" = pending ] || break
  done
  [ "$STATE" = accepted ] || { echo "Спарювання не підтверджено ($STATE). Нічого не змінено." >&2; exit 1; }
  ( umask 077; printf 'HOST=%s\nKEY=%s\nSECRET=%s\n' "${AGENTLIGHT_HOST:-agentlight.local}" "$KEY" "$SECRET" > "$DIR/allow.conf" )
  if [ "$TOOL" = claude ]; then
    curl -4 -fsS -m 10 --compressed "$LAMP/setup/allow.sh" -o "$DIR/allow.sh" && chmod +x "$DIR/allow.sh"
    SRC="$LAMP/setup/claude-allow.json"
  fi
  echo "Спаровано. «Дозволити» ще треба ввімкнути на сторінці лампи: Пристрій → Дозволити."
fi

NEW=$(mktemp)
trap 'rm -f "$NEW"' EXIT
curl -4 -fsS -m 10 --compressed "$SRC" -o "$NEW" || { echo "Не вдалося завантажити $SRC — лампа ввімкнена і в цій самій мережі?" >&2; exit 1; }
mkdir -p "$(dirname "$FILE")"
[ -s "$FILE" ] && cp "$FILE" "$FILE.bak-agentlight-$(date +%Y%m%d-%H%M%S)"

if [ "$MODE" = file ] || [ ! -s "$FILE" ]; then
  cp "$NEW" "$FILE"
elif command -v python3 >/dev/null 2>&1; then
  python3 - "$FILE" "$NEW" "$MODE" <<'PY'
import json, sys
target, new, mode = sys.argv[1:4]
cur, add = json.load(open(target)), json.load(open(new))
if mode == "key":                       # Antigravity: наш блок лежить під власним ключем
    cur.update(add)
else:                                   # решта: {"hooks": {подія: [записи]}} — прибираємо свої старі записи, дописуємо нові
    hooks = cur.setdefault("hooks", {})
    for event, entries in add.get("hooks", {}).items():
        hooks[event] = [e for e in hooks.get(event, []) if "agentlight" not in json.dumps(e)] + entries
    for key, value in add.items():
        if key != "hooks":
            cur.setdefault(key, value)
json.dump(cur, open(target, "w"), indent=2, ensure_ascii=False)
PY
elif command -v node >/dev/null 2>&1; then
  node - "$FILE" "$NEW" "$MODE" <<'JS'
const fs = require("fs"), [target, fresh, mode] = process.argv.slice(2);
const cur = JSON.parse(fs.readFileSync(target, "utf8")), add = JSON.parse(fs.readFileSync(fresh, "utf8"));
if (mode === "key") Object.assign(cur, add);
else {
  cur.hooks = cur.hooks || {};
  for (const [event, entries] of Object.entries(add.hooks || {}))
    cur.hooks[event] = (cur.hooks[event] || []).filter((e) => !JSON.stringify(e).includes("agentlight")).concat(entries);
  for (const [key, value] of Object.entries(add)) if (key !== "hooks" && !(key in cur)) cur[key] = value;
}
fs.writeFileSync(target, JSON.stringify(cur, null, 2) + "\n");
JS
else
  echo "У $FILE вже є налаштування, а для їх об'єднання потрібен python3 або node." >&2
  echo "Додай уручну вміст $SRC у $FILE." >&2
  exit 1
fi
echo "Готово: $TOOL підключено до лампи ($FILE). Перезапусти агента, щоб хуки запрацювали."
