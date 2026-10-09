# Підключення агентів

Лампа сама містить усе потрібне: готові хуки для кожного агента і встановлювач. Репозиторій для підключення не потрібен.

## Одна команда

На сторінці лампи: «Підключення агентів» → вибрати агента → скопіювати команду. Вона виглядає так:

```sh
curl -4 -fsS http://agentlight.local/install.sh | sh -s -- claude
```

Замість `claude` — `opencode`, `codex`, `gemini`, `antigravity`, `copilot`, `qwen` або `cursor`. Команду можна виконати самому в терміналі або дати агентові й попросити виконати. Після цього агента треба перезапустити.

Що робить встановлювач:

- завантажує з лампи хуки для вибраного агента;
- дописує їх до його налаштувань, не чіпаючи чужих хуків; повторний запуск замінює свої старі записи, а не дублює;
- попередню версію файлу лишає поруч як `*.bak-agentlight-<час>`.

Що потрібно на комп'ютері: `curl` і, якщо в агента вже є налаштування, `python3` або `node` для їх об'єднання. Без них встановлювач нічого не змінює і показує, який файл додати вручну. Windows без WSL не підтримується.

| Агент | Куди ставиться |
|---|---|
| Claude Code | `~/.claude/settings.json` |
| OpenCode | `~/.config/opencode/plugins/agentlight.js` |
| Codex CLI | `~/.codex/hooks.json` |
| Gemini CLI | `~/.gemini/settings.json` |
| Antigravity | `~/.gemini/config/hooks.json` |
| Copilot CLI | `~/.copilot/hooks/agentlight.json` |
| Qwen Code | `~/.qwen/settings.json` |
| Cursor | `~/.cursor/hooks.json` |

## Дозволити з лампи (експериментально)

Те саме встановлення зі словом `allow` — лише для `claude` й `opencode`:

```sh
curl -4 -fsS http://agentlight.local/install.sh | sh -s -- claude allow
```

Команда спаровує комп'ютер із лампою (попросить торкнутись її), кладе секрет у `~/.agentlight/allow.conf` і додає хук, який питає лампу про дозвіл. Що це таке і які має запобіжники — у [firmware.md](firmware.md), розділ «Дозволити».

## Як це влаштовано

Хук — це один виклик `curl`: він бере перші 4 КБ події агента і шле їх лампі як є.

```sh
d=$(head -c 4000); printf %s "$d" | curl -4 -s -m 2 -o /dev/null -X POST -H 'Content-Type: application/json' \
  --data-binary @- http://agentlight.local/hook/claude/busy >/dev/null 2>&1 &
```

Лампа сама дістає з події сесію, теку проєкту, запит і поточну дію (`firmware/src/hook.cpp`). Тому на комп'ютері немає ні скриптів, ні `jq`, а запит іде у фоні з обмеженням 2 секунди й агента не гальмує.

Адреса запиту: `/hook/<агент>/<стан>`. Стан — `busy`, `waiting`, `done`, `error`, `idle` або псевдостан, який лампа уточнює за вмістом події:

- `stop` — кінець ходу з причиною (Cursor, Antigravity): успіх, помилка, переривання або «фонові задачі ще йдуть»;
- `notification` — сповіщення: «чекає» лише для запиту дозволу чи питання, решта ігнорується.

OpenCode підключається інакше — плагіном, який працює всередині нього і шле стан на `/api/status`.

Хуки звертаються до лампи за іменем `agentlight.local`. Якщо в мережі воно не працює (частина Linux-систем без mDNS), заміни його у встановленому файлі на IP-адресу лампи.

## Які події який стан дають

| Агент | працює | чекає | готово | помилка | спокій |
|---|---|---|---|---|---|
| Claude Code | UserPromptSubmit, PostToolUse, PostToolUseFailure, PermissionDenied, ElicitationResult, PreCompact | PermissionRequest, PreToolUse (AskUserQuestion), Elicitation, Notification | Stop | StopFailure | SessionEnd |
| OpenCode | chat.message, tool.execute.before, session.status, permission.replied | permission.asked, question.asked | session.status idle | session.error | session.deleted, переривання |
| Codex CLI | UserPromptSubmit, PostToolUse | PermissionRequest | Stop | — | Interrupt, SessionEnd |
| Gemini CLI | BeforeAgent, BeforeTool | Notification | AfterAgent | — | SessionEnd |
| Antigravity | PreInvocation | — | Stop | Stop з помилкою | — |
| Copilot CLI | userPromptSubmitted, postToolUse, postToolUseFailure | notification | agentStop | errorOccurred | sessionEnd |
| Qwen Code | UserPromptSubmit, PostToolUse, PostToolUseFailure, PermissionDenied | PermissionRequest, Notification | Stop | StopFailure | SessionEnd |
| Cursor | beforeSubmitPrompt, postToolUse, postToolUseFailure | — | stop | stop з помилкою | sessionEnd, переривання |

## Що перевірено

| Агент | Стан перевірки |
|---|---|
| Claude Code | усі п'ять станів на справжній програмі: «працює» із запитом, дія інструмента, «чекає» з назвою дії, «готово», «помилка», «спокій» після закриття |
| OpenCode | «працює» з назвою інструмента і «готово» на справжньому OpenCode; запит дозволу, питання, помилка й переривання — ні |
| Antigravity | «працює» і «готово» на справжньому Antigravity IDE 2.5.5, вручну; помилка й фонові задачі — лише пробними подіями |
| Codex CLI | «працює» (із запитом і назвою проєкту) і закриття сесії на справжньому Codex без входу в акаунт; «готово» й «чекає» — лише пробними подіями |
| Qwen Code | «працює» на справжньому Qwen Code без входу в акаунт; решта — ні |
| Copilot CLI | на справжньому не перевірено: без входу в GitHub він не запускає сесію; лише пробні події |
| Cursor | лише пробними подіями: програми немає |
| Gemini CLI | не перевірено: не запускається без платного ключа |

Claude Code, Codex, Qwen і OpenCode перевіряються автоматично: щотижня на свіжих версіях ([testing.md](testing.md)) і в наскрізній перевірці лампи ([e2e-report.md](e2e-report.md)).

Встановлювач для всіх восьми агентів перевірений у порожній домашній теці: файли лягають на місця, чужі хуки зберігаються, повторний запуск нічого не дублює.

Відомі особливості:

- **OpenCode з плагіном oh-my-openagent сам запускає хуки Claude Code**, але без події завершення. Лампа такі виклики пропускає (сесії з ідентифікатором `ses_…`); їх веде плагін OpenCode.
- **Antigravity не має стану «чекає».** Єдиний спосіб його зловити — хук перед інструментом, а він мусить повертати рішення про дозвіл, і з документації незрозуміло, яке безпечне.
- **Codex не запускає нові хуки, доки їх не схвалено.** Після встановлення він при першому запуску попросить переглянути хуки й довірити їм — без цього лампа мовчатиме.
- **Qwen Code при помилці з'єднання з сервером не шле ні помилки, ні завершення сесії** — лампа лишається в «працює», доки не спливе час цього стану.
- **Cursor** за документацією сам імпортує хуки з `~/.claude/settings.json`; тоді сесії Cursor з'являться на лампі як сесії Claude Code, і окремі хуки Cursor дадуть дублі.

## Перевірка без агента

```sh
echo '{"session_id":"test1234","cwd":"/tmp/demo","hook_event_name":"UserPromptSubmit","prompt":"привіт"}' \
  | curl -4 -s -X POST -H 'Content-Type: application/json' --data-binary @- http://agentlight.local/hook/claude/busy
```

Лампа має показати «працює», а на її сторінці з'явиться картка `demo`.

## Звідки файли

Джерело — тека `hooks/` репозиторію: по одному файлу на агента і `install.sh`. Під час збірки прошивки `firmware/tools/embed_assets.py` пакує їх у прошивку, і лампа роздає їх за адресами `/setup/<файл>` та `/install.sh`.
