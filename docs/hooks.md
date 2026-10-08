# Хуки Claude Code

`hooks/agentlight.sh` — скрипт, який Claude Code викликає на подіях сесії. Він читає JSON події і шле стан на лампу.

| Подія | Стан |
|---|---|
| UserPromptSubmit, PostToolUse, PostToolUseFailure | працює |
| PermissionRequest, PreToolUse (AskUserQuestion), Notification (дозвіл або питання) | чекає |
| Stop | готово |
| SessionEnd | вимкнено |

Що передається на лампу: `agent_id` (перші 8 символів `session_id`, тож паралельні сесії не затирають одна одну), назва теки проєкту, запит користувача й поточна дія — інструмент чи питання.

## Встановлення

Потрібні `jq` і `curl`.

```sh
mkdir -p ~/.claude/hooks
cp hooks/agentlight.sh ~/.claude/hooks/
chmod +x ~/.claude/hooks/agentlight.sh
```

Далі додай вміст `hooks/settings.example.json` у розділ `hooks` файлу `~/.claude/settings.json`. Якщо там уже є хуки на ті самі події — дописуй у наявні масиви, а не заміняй їх.

Хуки запускаються у фоні (`async`) з обмеженням 5 с, тож не гальмують Claude Code, навіть коли лампа вимкнена.

## Адреса лампи

Типово `agentlight.local`. Іншу адресу задає змінна середовища:

```sh
export AGENTLIGHT_HOST=192.168.1.50
```

## Перевірка

```sh
echo '{"session_id":"test1234","cwd":"/tmp/demo","hook_event_name":"UserPromptSubmit","prompt":"привіт"}' \
  | ~/.claude/hooks/agentlight.sh busy
```

Лампа має засвітитись оранжевим, а на її сторінці з'явиться картка `demo`.
