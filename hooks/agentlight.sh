#!/bin/sh
# Хук Claude Code: шле стан агента на AgentLight. Виклик: agentlight.sh busy|waiting|done|error|idle
# З JSON на stdin бере: session_id (agent_id, щоб паралельні сесії не затирали одна одну),
# теку проєкту (name), запит юзера (task) і поточну дію — інструмент чи питання (message).
# Встановлення: cp hooks/agentlight.sh ~/.claude/hooks/  (шляхи в ~/.claude/settings.json ведуть туди)
HOST="${AGENTLIGHT_HOST:-agentlight.local}"
body=$(jq -c --arg state "$1" '
  def cut($n): gsub("\\s+"; " ") | if length > $n then .[0:$n] + "…" else . end;
  (.tool_input // {}) as $in
  | ($in.description // ($in.file_path // "" | split("/") | last) // $in.pattern // $in.command // "") as $what
  | ([.tool_name, $what] | map(select(. != null and . != "")) | join(": ")) as $tool
  | {state: $state,
     agent_id: ("claude-" + ((.session_id // "x")[0:8])),
     name: ((.cwd // "") | split("/") | last // ""),
     message: (
       if .hook_event_name == "UserPromptSubmit" then "думає"
       elif .hook_event_name == "PermissionRequest" then "Дозвіл — " + $tool
       elif .hook_event_name == "PreToolUse" then ($in.questions[0].question // $tool)
       elif .hook_event_name == "Notification" then (.message // "")
       elif .hook_event_name == "Elicitation" then (.message // "питання від MCP-сервера")
       elif .hook_event_name == "StopFailure" then ("Помилка: " + (.error // "невідома"))
       elif .hook_event_name == "PreCompact" then "стискає контекст"
       elif $state == "busy" then $tool
       else "" end | cut(100))}
  + (if .prompt then {task: (.prompt | cut(160))} else {} end)' 2>/dev/null)
[ -n "$body" ] || body="{\"state\":\"$1\",\"agent_id\":\"claude-x\"}"
# -4: без цього macOS 5 с чекає на IPv6-відповідь mDNS для .local
curl -4 -s -m 2 -o /dev/null -X POST -H 'Content-Type: application/json' -d "$body" "http://$HOST/api/status"
exit 0
