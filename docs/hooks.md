# Підключення агентів

Лампа отримує стан від самого інструмента: на подіях сесії він запускає наш скрипт або плагін, а той шле запит на лампу. Модель станів і порівняння інструментів — у [agents.md](agents.md).

| Інструмент | Що ставити | Стан |
|---|---|---|
| Claude Code | `hooks/agentlight.sh` + `hooks/claude/settings.json` | перевірено на справжньому інструменті |
| OpenCode | `hooks/opencode/agentlight.js` | перевірено на справжньому інструменті (1.18.35) |
| Gemini CLI | `hooks/agentlight.sh` + `hooks/gemini/settings.json` | перевірено лише тестовими подіями |
| Antigravity | `hooks/agentlight.sh` + `hooks/antigravity/hooks.json` | перевірено лише тестовими подіями |
| Codex CLI | `hooks/agentlight.sh` + `hooks/codex/hooks.json` | перевірено лише тестовими подіями |
| Copilot CLI | `hooks/agentlight.sh` + `hooks/copilot/agentlight.json` | перевірено лише тестовими подіями |
| Qwen Code | `hooks/agentlight.sh` + `hooks/qwen/settings.json` | перевірено лише тестовими подіями |
| Cursor | `hooks/agentlight.sh` + `hooks/cursor/hooks.json` | перевірено лише тестовими подіями |
| Kilo Code | `hooks/opencode/agentlight.js` (ті самі назви подій) | не перевірено |

«Тестовими подіями» означає: скриптові подано зразок JSON у форматі цього інструмента, і лампа показала правильний стан. Чи сам інструмент викличе хук саме так, як описано в його документації, не перевірено.

## Скрипт

`hooks/agentlight.sh <стан> [інструмент]` — один на всі інструменти з хуками-командами. Потрібні `jq` і `curl`.

- **Стан:** `busy`, `waiting`, `done`, `error`, `idle`. Є два псевдостани, які скрипт сам перетворює за вмістом події: `stop` (кінець ходу з причиною — Cursor, Antigravity) і `notification` (стає `waiting` лише для запиту дозволу чи питання, решта сповіщень ігнорується).
- **Інструмент:** `claude` (типово), `codex`, `gemini`, `agy`, `cursor`, `copilot`, `qwen`. Іде в `agent_id`: `claude-1a2b3c4d`, `codex-…`. Для `gemini` і `agy` скрипт ще друкує `{}`, бо вони чекають JSON у відповідь.
- **Що шле:** `agent_id` (інструмент і перші 8 символів ідентифікатора сесії, тож паралельні сесії не затирають одна одну), назву теки проєкту, запит користувача й поточну дію.
- **Агента не гальмує:** запит іде у фоні з обмеженням 2 с; якщо лампа вимкнена, нічого не станеться.

Встановлення скрипта для всіх інструментів, крім Claude Code:

```sh
mkdir -p ~/.agentlight
cp hooks/agentlight.sh ~/.agentlight/ && chmod +x ~/.agentlight/agentlight.sh
```

Адресу лампи (типово `agentlight.local`) міняє змінна середовища:

```sh
export AGENTLIGHT_HOST=192.168.1.50
```

Файли з теки `hooks/<інструмент>/` — це лише розділ хуків. Якщо у твоєму файлі налаштувань уже є хуки на ті самі події, дописуй наші в наявні масиви, а не заміняй їх.

## Claude Code

```sh
mkdir -p ~/.claude/hooks
cp hooks/agentlight.sh ~/.claude/hooks/ && chmod +x ~/.claude/hooks/agentlight.sh
```

Вміст `hooks/claude/settings.json` додай у розділ `hooks` файлу `~/.claude/settings.json`.

| Стан | Події |
|---|---|
| працює | UserPromptSubmit, PostToolUse, PostToolUseFailure, PermissionDenied, ElicitationResult, PreCompact |
| чекає | PermissionRequest, PreToolUse (AskUserQuestion), Elicitation, Notification (дозвіл, питання, фонова сесія чекає) |
| готово | Stop |
| помилка | StopFailure |
| спокій | SessionEnd |

Хуки запускаються у фоні (`async`) з обмеженням 5 с.

## OpenCode

```sh
mkdir -p ~/.config/opencode/plugins
cp hooks/opencode/agentlight.js ~/.config/opencode/plugins/
```

Для одного проєкту — у `.opencode/plugins/`. Плагін працює всередині OpenCode, скрипт і `jq` не потрібні.

| Стан | Події |
|---|---|
| працює | `chat.message`, `session.status` (busy, retry), `tool.execute.before`, `permission.replied`, `question.replied`, `question.rejected`, стискання контексту |
| чекає | `permission.asked`, `question.asked` |
| готово | `session.status` idle, якщо в цьому ході не було помилки |
| помилка | `session.error`, крім двох випадків нижче |
| спокій | `session.deleted`; `session.error` з `MessageAbortedError` — користувач перервав |

Що перевірено на OpenCode 1.18.35 командою `opencode run`: лампа показала «працює» з текстом запиту й назвою інструмента, потім «готово»; назва проєкту й окремий `agent_id` на сесію правильні. Не перевірено: запит дозволу, питання, помилка, переривання, підагенти — для них у неінтерактивному запуску не було нагоди.

Особливості:

- **`ContextOverflowError` не вважається помилкою:** після неї OpenCode зазвичай сам стискає контекст і продовжує.
- **Сесії підагентів ігноруються** (за `parentID`), інакше лампа блимала б «готово» посеред ходу.
- **Закриття OpenCode події не дає:** лампа лишиться на «готово», доки не спливе час цього стану.
- **OpenCode з плагіном oh-my-openagent сам запускає хуки Claude Code** з `~/.claude/settings.json`, але без події завершення — раніше це лишало на лампі завислу сесію «працює». Тепер `agentlight.sh` такі виклики пропускає (ідентифікатор сесії починається з `ses_`), а стан веде плагін.
- **`opencode run` у скриптах запускай із `</dev/null`:** без цього він чекає на stdin і не завершується. До лампи це стосунку не має.

## Gemini CLI

Розділ `hooks` з `hooks/gemini/settings.json` додай у `~/.gemini/settings.json`.

| Стан | Події |
|---|---|
| працює | BeforeAgent, BeforeTool |
| чекає | Notification (`ToolPermission`) |
| готово | AfterAgent |
| спокій | SessionEnd |

Події про помилку ходу Gemini CLI не має. На справжньому інструменті не перевірено: з червня 2026 він працює лише з платними API-ключами.

## Antigravity

`hooks/antigravity/hooks.json` поклади як `~/.gemini/config/hooks.json` (усі проєкти) або `.agents/hooks.json` (один проєкт). Якщо такий файл уже є, додай у нього наш ключ `agentlight`. Хуки спільні для IDE, застосунку Antigravity 2.0 і CLI `agy`.

| Стан | Події |
|---|---|
| працює | PreInvocation; PostToolUse для `ask_question` і `ask_permission`; Stop із `fullyIdle: false` |
| готово | Stop, `terminationReason: "model_stop"` |
| помилка | Stop з іншою причиною або з полем `error` |

Стану «чекає» немає: хук `PreToolUse` в Antigravity мусить повернути рішення про дозвіл, і невідомо, яке з них не змінить поведінку агента, тож ми його не чіпаємо. Подій початку й кінця сесії та тексту запиту Antigravity не дає. На справжньому інструменті не перевірено: агент IDE запускається лише з вікна програми.

## Codex CLI

Вміст `hooks/codex/hooks.json` додай у `~/.codex/hooks.json`.

| Стан | Події |
|---|---|
| працює | UserPromptSubmit, PostToolUse |
| чекає | PermissionRequest |
| готово | Stop |
| спокій | Interrupt, SessionEnd |

Окремої події про помилку немає.

## Copilot CLI

`hooks/copilot/agentlight.json` поклади в `~/.copilot/hooks/`.

| Стан | Події |
|---|---|
| працює | userPromptSubmitted, postToolUse, postToolUseFailure |
| чекає | notification (`permission_prompt`, `elicitation_dialog`) |
| готово | agentStop |
| помилка | errorOccurred |
| спокій | sessionEnd |

## Qwen Code

Розділ `hooks` з `hooks/qwen/settings.json` додай у `~/.qwen/settings.json`. Події ті самі, що в Claude Code, включно з `StopFailure` для помилки.

## Cursor

Вміст `hooks/cursor/hooks.json` додай у `~/.cursor/hooks.json`.

| Стан | Події |
|---|---|
| працює | beforeSubmitPrompt, postToolUse, postToolUseFailure |
| готово | stop зі статусом `completed` |
| помилка | stop зі статусом `error` |
| спокій | stop зі статусом `aborted`, sessionEnd |

Події очікування дозволу Cursor не має.

За документацією Cursor типово сам імпортує хуки з `~/.claude/settings.json` (Settings → Agents → Third-Party Imports). Тоді хуки Claude Code спрацюють і в ньому, і одна розмова з'явиться на лампі двічі: як `claude-…` і як `cursor-…`. Щоб цього не було, або вимкни цей імпорт у Cursor, або не став `hooks/cursor/hooks.json` і покладайся на імпортовані хуки (тоді не буде розрізнення «готово» і «помилка»). Не перевірено: Cursor на цій машині не встановлений.

## Перевірка без інструмента

```sh
echo '{"session_id":"test1234","cwd":"/tmp/demo","hook_event_name":"UserPromptSubmit","prompt":"привіт"}' \
  | ~/.agentlight/agentlight.sh busy codex
```

Лампа має засвітитись як «працює», а на її сторінці з'явиться картка `demo` з агентом `codex-test1234`. Прибрати її: `curl -4 -X POST 'http://agentlight.local/api/status?state=idle&agent_id=codex-test1234'`.

Щоб побачити запит і нічого не слати, додай `AGENTLIGHT_DRY_RUN=1` перед командою.
