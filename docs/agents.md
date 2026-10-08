# Стани лампи й підтримка агентів

## П'ять станів

Лампа знає рівно п'ять станів. Агент шле один із них; якщо агентів кілька, лампа показує найважливіший (у таблиці — знизу вгору).

| Стан | Назва на сторінці | Значення |
|---|---|---|
| `idle` | спокій | нічого не відбувається: агентів немає або сесію закрито |
| `done` | готово | агент завершив хід успішно |
| `busy` | працює | агент щось робить |
| `waiting` | чекає на тебе | агентові потрібен дозвіл або відповідь |
| `error` | помилка | хід обірвався помилкою |

Один запит — одна зміна стану:

```sh
curl -4 -X POST http://agentlight.local/api/status -H 'Content-Type: application/json' \
  -d '{"state":"busy","agent_id":"<сесія>","name":"<проєкт>","task":"<запит>","message":"<що робить>"}'
```

## Claude Code — підтримується

Хуки з `hooks/claude.json` шлють кожну подію лампі на `/hook/claude/<стан>`; встановлення — у [hooks.md](hooks.md).

| Стан | Події |
|---|---|
| `busy` | `UserPromptSubmit`, `PostToolUse`, `PostToolUseFailure`, `PermissionDenied`, `ElicitationResult`, `PreCompact` |
| `waiting` | `PermissionRequest`, `PreToolUse` (AskUserQuestion), `Elicitation`, `Notification` (`permission_prompt`, `elicitation_dialog`, `elicitation_url_dialog`, `agent_needs_input`) |
| `done` | `Stop` |
| `error` | `StopFailure` — ліміт, перевантаження, збій API, проблема з оплатою чи входом; `Stop` тоді не спрацьовує |
| `idle` | `SessionEnd` |

Чого Claude Code не повідомляє:

- **Користувач перервав агента (Esc).** Події немає; лампа лишається в `busy`, доки не спливе час цього стану.
- **Користувач дав дозвіл.** Події немає; лампа лишається в `waiting`, доки інструмент не відпрацює.

`PreToolUse` для всіх інструментів навмисно не підключений: він спрацьовує майже одночасно з `PermissionRequest`, а хуки асинхронні, тож `busy` міг би прийти після `waiting` і затерти його.

## Інші агенти

Готові файли й кроки встановлення — у [hooks.md](hooks.md). Що з цього справді запускалось:

| Інструмент | Стан інтеграції |
|---|---|
| OpenCode | перевірено на справжньому інструменті: «працює» і «готово»; решта станів — ні |
| Gemini CLI, Antigravity, Codex CLI, Copilot CLI, Qwen Code, Cursor | перевірено лише тестовими подіями: скрипт правильно розбирає їхній формат, але самі інструменти не запускались |
| Kilo Code | не перевірено; має підійти плагін OpenCode |
| решта таблиці нижче | нічого не зроблено, лише дослідження |

### Що вміє кожен інструмент

Зведення з документації й вихідного коду станом на 2026-10-08. «так» — є подія; «частково» — із застереженням; «ні» — події немає.

| Інструмент | busy | waiting | done | error | idle | Механізм |
|---|---|---|---|---|---|---|
| **OpenCode** | так | так | так | так | частково | плагін-файл у `~/.config/opencode/plugins/` |
| **Antigravity** (IDE, 2.0, `agy`) | так | частково | так | так | ні | `~/.gemini/config/hooks.json` |
| **Gemini CLI** | так | так | так | ні | так | `hooks` у `~/.gemini/settings.json` |
| **Codex CLI** | так | частково | так | ні | так | `~/.codex/hooks.json` |
| **Copilot CLI** | так | так | так | так | так | `~/.copilot/hooks/*.json` |
| **Qwen Code** | так | так | так | так | так | `hooks` у `~/.qwen/settings.json` |
| **Kilo Code** | так | так | так | так | частково | плагін у `~/.config/kilo/plugin/` |
| **Cursor** (IDE) | так | ні | так | так | так | `~/.cursor/hooks.json` |
| **Cline** (CLI) | так | ні | так | так | так | плагін у `~/.cline/plugins/` |
| **Goose** | так | ні | так | частково | так | `~/.agents/plugins/<назва>/hooks/hooks.json` |
| **Copilot у VS Code** | так | ні | так | ні | ні | `.github/hooks/*.json` |
| **Windsurf** | так | ні | так | ні | ні | `~/.codeium/windsurf/hooks.json` |
| **Kiro** | так | ні | так | ні | частково | `.kiro/hooks/*.json` |
| **Amp** | так | ні | так | ні | ні | плагін у `~/.config/amp/plugins/` |
| **Crush** | так | ні | ні | ні | ні | `PreToolUse` у `crush.json` |
| **Aider** | ні | частково | частково | ні | ні | `--notifications-command`: один сигнал «готовий до вводу» |
| **Zed** | ні | ні | ні | ні | ні | хуків немає |

### Деталі по головних

**OpenCode.** Плагін — один JS-файл, що працює всередині процесу OpenCode і отримує події: `session.status` (busy / idle / retry), `permission.asked` і `permission.replied`, `question.asked`, `session.error`, `session.deleted`. Запит користувача дає хук `chat.message`. На відміну від Claude Code, видно і «дозвіл надано», і переривання користувачем (`session.error` з `MessageAbortedError`). Нюанси: успіх і помилку треба розрізняти самому (після `session.error` теж приходить idle); дочірні сесії підагентів треба відсіювати за `parentID`; події закриття програми не знайдено.

**Antigravity.** Одна система хуків на IDE, настільний застосунок і CLI `agy`. Події: `PreInvocation`, `PostInvocation`, `PreToolUse`, `PostToolUse`, `Stop`. Успіх і помилка розрізняються полем `terminationReason` у `Stop`. Прогалини: немає події для звичайного запиту дозволу (лише інструменти `ask_question` і `ask_permission`), немає початку й кінця сесії, немає тексту запиту. На форумі є скарга, що хуки в одній із версій IDE не спрацьовували.

**Gemini CLI.** Події: `SessionStart`, `BeforeAgent` (з текстом запиту), `BeforeTool`, `AfterTool`, `Notification` (запит дозволу), `AfterAgent`, `SessionEnd`. Події про помилку ходу немає. З червня 2026 Gemini CLI працює лише з платними API-ключами; Google переводить користувачів на Antigravity CLI.

**Codex CLI.** Хуки у стилі Claude Code: `UserPromptSubmit`, `PreToolUse`, `PermissionRequest`, `PostToolUse`, `Stop`, `Interrupt`, `SessionEnd`. Окремої події про помилку немає. Питання в режимі планування йдуть лише як термінальні сповіщення.

**Cursor.** Власний `hooks.json`; `stop` має статус `completed`, `error` або `aborted`. Події очікування дозволу немає. Cursor типово читає й `~/.claude/settings.json`, тож наявні хуки Claude Code можуть спрацьовувати і в ньому.

### Спільне

- **Формат хуків Claude Code став фактичним стандартом.** Codex, Qwen Code, Goose, Crush і Copilot повторюють його назви подій і JSON на stdin; Cursor, Copilot CLI і VS Code вміють читати файли налаштувань Claude. Один скрипт-відправник може обслуговувати більшість інструментів; відрізняється лише файл налаштувань.
- **Плагіни замість хуків** мають OpenCode, Kilo Code, Cline і Amp — їм потрібен окремий JS/TS-файл.
- **MCP** (`set_status` на лампі) працює з будь-яким агентом, але викликає його модель, а не програма, тож стан залежить від того, чи модель про це згадає. Годиться як доповнення, не як основа.
- **Термінальні сигнали** (дзвінок, OSC 9 / 777) дають лише один злитий сигнал «потрібна увага» — запасний варіант для інструментів без хуків.

### Що з'ясувалось під час перевірки

- **OpenCode з плагіном oh-my-openagent запускає хуки Claude Code** з `~/.claude/settings.json` зі своїм ідентифікатором сесії, але без події завершення. До виправлення кожна сесія OpenCode лишала на лампі завислий запис «працює» на 10 хвилин. Тепер скрипт такі виклики пропускає.
- **Gemini CLI на безкоштовному обліковому записі не запускається** («This client is no longer supported»), тож перевірити його не вдалося.
- **Antigravity IDE не має неінтерактивного режиму**, а його хук `PreToolUse` мусить повертати рішення про дозвіл — тому стан «чекає» для нього не зроблено.
