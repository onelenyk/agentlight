#!/bin/sh
# Хук для AI-агентів: шле стан на лампу AgentLight. Один скрипт на всі інструменти з хуками-командами.
#
#   agentlight.sh <стан> [інструмент]
#
# <стан>: busy | waiting | done | error | idle — або псевдостан, який скрипт сам перетворює за JSON події:
#   stop         — кінець ходу з причиною: Cursor (status), Antigravity (terminationReason, fullyIdle)
#   notification — сповіщення: «чекає» лише для запиту дозволу чи питання, решта ігнорується
# [інструмент]: claude (типово) | codex | gemini | agy | cursor | copilot | qwen | ... — префікс agent_id;
#   для gemini й agy скрипт ще друкує "{}" у stdout, бо ці інструменти чекають там JSON.
#
# З JSON на stdin бере: ідентифікатор сесії (agent_id, щоб паралельні сесії не затирали одна одну),
# теку проєкту (name), запит юзера (task) і поточну дію — інструмент чи питання (message).
# Назви полів у різних інструментів різні, тому кожне шукається в кількох варіантах.
# Агента не гальмує: запит іде у фоні з обмеженням 2 с, код виходу завжди 0.
# Встановлення: див. docs/hooks.md.
HOST="${AGENTLIGHT_HOST:-agentlight.local}"
STATE="$1"
TOOL="${2:-${AGENTLIGHT_TOOL:-claude}}"

case "$TOOL" in gemini|agy) printf '{}\n' ;; esac   # цим інструментам потрібен JSON у stdout, решті — порожньо

body=$(jq -c --arg state "$STATE" --arg tool "$TOOL" --arg pwd "$PWD" \
          --arg envsid "${GEMINI_SESSION_ID:-${CRUSH_SESSION_ID:-}}" '
  def cut($n): gsub("\\s+"; " ") | if length > $n then .[0:$n] + "…" else . end;
  def str: if type == "string" then . else "" end;
  (.session_id // .sessionId // .conversationId // .conversation_id // .trajectory_id
     | if type == "string" and . != "" then . elif $envsid != "" then $envsid else "x" end) as $sid
  | (.cwd // .workspacePaths[0]? // .workspace_roots[0]? // .working_dir // $pwd | str) as $cwd
  | (.hook_event_name // .hookEventName // .event // "" | str) as $event
  | (.tool_name // .toolName // .toolCall.name? // "" | str) as $name
  | (.tool_input // .toolArgs // .toolCall.args? // {}
     | if type == "string" then (try fromjson catch {}) else . end | if type == "object" then . else {} end) as $in
  | ($in.description // ($in.file_path // "" | split("/") | last) // $in.pattern // $in.command // "" | str) as $what
  | ([$name, $what] | map(select(. != null and . != "")) | join(": ")) as $tooltext
  | (.notification_type // .notificationType // "" | str) as $ntype
  | (.status // .terminationReason // "" | str) as $why
  | (.error | if type == "object" then (.message // "") else . end | str) as $err
  | (if $state == "stop" then
       if $why == "aborted" then "idle"                                    # Cursor: користувач перервав
       elif $why == "error" or $err != "" or $why == "max_steps_exceeded" then "error"
       elif .fullyIdle == false then "busy"                                # Antigravity: фонові задачі ще йдуть
       else "done" end
     elif $state == "notification" then
       if ($ntype | test("^(permission_prompt|elicitation_dialog|elicitation_url_dialog|agent_needs_input|ToolPermission)$"))
       then "waiting" else "skip" end
     else $state end
     # OpenCode з oh-my-openagent запускає хуки Claude Code зі своїм session_id (ses_...), але без Stop:
     # лампа зависла б у «працює». Таку сесію веде плагін hooks/opencode/agentlight.js, тут її пропускаємо.
     | if $tool == "claude" and ($sid | startswith("ses_")) then "skip" else . end) as $final
  | {state: $final,
     agent_id: ($tool + "-" + $sid[0:8]),
     name: ($cwd | split("/") | map(select(. != "")) | last // ""),
     message: (
       if $final == "error" then ("Помилка: " + (if $err != "" then $err elif $why != "" then $why else "невідома" end))
       elif ($event | test("^(UserPromptSubmit|userPromptSubmitted|beforeSubmitPrompt|BeforeAgent|PreInvocation)$")) then "думає"
       elif ($event | test("^(PermissionRequest|permissionRequest)$")) then "Дозвіл — " + $tooltext
       elif $event == "PreToolUse" and $final == "waiting" then ($in.questions[0].question? // $tooltext)
       elif ($event | test("^[Nn]otification$")) or $state == "notification" then (.message // "" | str)
       elif $event == "Elicitation" then (.message // "питання від MCP-сервера" | str)
       elif $event == "PreCompact" or $event == "preCompact" then "стискає контекст"
       elif $final == "waiting" and $tooltext != "" then "Дозвіл — " + $tooltext
       elif $final == "busy" then $tooltext
       else "" end | cut(100))}
  + (if (.prompt | type) == "string" and .prompt != "" then {task: (.prompt | cut(160))} else {} end)' 2>/dev/null)
[ -n "$body" ] || body="{\"state\":\"$STATE\",\"agent_id\":\"$TOOL-x\"}"
case "$body" in *'"state":"skip"'*|*'"state":"stop"'*|*'"state":"notification"'*) exit 0 ;; esac

if [ -n "$AGENTLIGHT_DRY_RUN" ]; then printf '%s\n' "$body" >&2; exit 0; fi   # для перевірок: показати запит і не слати
# -4: без цього macOS 5 с чекає на IPv6-відповідь mDNS для .local. Усі потоки закриті, щоб агент не чекав на фоновий curl.
curl -4 -s -m 2 -o /dev/null -X POST -H 'Content-Type: application/json' -d "$body" "http://$HOST/api/status" \
  </dev/null >/dev/null 2>&1 &
exit 0
