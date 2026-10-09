#!/bin/sh
# «Дозволити» (експериментально): питає лампу AgentLight, чи дозволити агентові дію, якої він просить.
# Викликається хуком запиту дозволу: подія на stdin, рішення для агента — на stdout.
#   allow.sh claude
# Друкує рішення лише тоді, коли власник відповів дотиком і підпис лампи зійшовся. У будь-якому іншому
# разі (лампа мовчить, відмовилась, час вийшов, підпис не той) не друкує нічого — агент показує звичайне питання.
# Налаштування: ~/.agentlight/allow.conf з HOST, KEY і SECRET; його пише встановлювач після спарювання.
AGENT="${1:-claude}"
CONF="$HOME/.agentlight/allow.conf"
[ -r "$CONF" ] || exit 0                  # комп'ютер не спарований (сам `.` на відсутньому файлі валить sh)
. "$CONF"
[ -n "$HOST" ] && [ -n "$KEY" ] && [ -n "$SECRET" ] || exit 0
event=$(head -c 4000)
nonce=$(openssl rand -hex 8) || exit 0
answer=$(printf %s "$event" | curl -4 -s -m 3 -X POST -H 'Content-Type: application/json' --data-binary @- \
  "http://$HOST/api/allow/ask?agent=$AGENT&key=$KEY&nonce=$nonce") || exit 0
id=$(printf %s "$answer" | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p')
[ -n "$id" ] || exit 0                      # лампа відмовилась пропонувати: вимкнено, небезпечна команда, зайнята

tries=0
while [ $tries -lt 34 ]; do                 # лампа чекає дотику 15 секунд; питаємо двічі на секунду, із запасом
  sleep 0.5
  tries=$((tries + 1))
  answer=$(curl -4 -s -m 2 "http://$HOST/api/allow/ask?id=$id") || exit 0
  state=$(printf %s "$answer" | sed -n 's/.*"state": *"\([a-z]*\)".*/\1/p')
  [ "$state" = pending ] && continue
  case "$state" in allow|deny) ;; *) exit 0 ;; esac
  sig=$(printf %s "$answer" | sed -n 's/.*"sig": *"\([0-9a-f]*\)".*/\1/p')
  want=$(printf %s "$nonce:$state" | openssl dgst -sha256 -hmac "$SECRET" | sed 's/^.*[= ]//')
  [ -n "$sig" ] && [ "$sig" = "$want" ] || exit 0    # відповів хтось, хто не знає секрету: не лампа
  if [ "$state" = allow ]; then
    echo '{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}'
  else
    echo '{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny","message":"Відхилено з лампи AgentLight"}}}'
  fi
  exit 0
done
exit 0
