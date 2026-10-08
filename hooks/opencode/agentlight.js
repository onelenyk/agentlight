// AgentLight: плагін OpenCode, що шле стан агента на лампу. Kilo Code має ті самі назви подій.
// Встановлення: скопіювати цей файл у ~/.config/opencode/plugins/ (усі проєкти) або .opencode/plugins/ (один проєкт).
// Адресу лампи міняє змінна середовища AGENTLIGHT_HOST (типово agentlight.local).
//
// Подія OpenCode                                   → стан лампи
//   chat.message (запит користувача)               → busy, запит іде в task
//   session.status busy / retry                    → busy
//   tool.execute.before                            → busy, назва інструмента в message
//   permission.asked, question.asked               → waiting
//   permission.replied, question.replied/rejected  → busy
//   session.error                                  → error (MessageAbortedError — це Esc, тоді idle)
//   session.status idle                            → done, якщо в цьому ході не було помилки
//   session.deleted                                → idle
//
// У файлі не має бути інших експортів, крім функції плагіна: OpenCode вважає плагіном кожен експорт.

export const AgentLight = async ({ directory, worktree }) => {
  const url = `http://${process.env.AGENTLIGHT_HOST ?? "agentlight.local"}/api/status`
  const name = (worktree && worktree !== "/" ? worktree : directory).split("/").filter(Boolean).pop() ?? "opencode"
  const children = new Set()   // сесії підагентів: їхній idle — не кінець ходу, тому мовчимо про них
  const failed = new Set()     // сесії, де цей хід уже закінчився помилкою чи перериванням
  const asking = new Set()     // сесії, що чекають відповіді: busy від session.status не має затерти waiting
  const tasks = new Map()
  const pending = new Set()    // запити в дорозі: dispose дочекається їх, щоб «готово» не загубилось при виході
  const shown = new Map()      // останній надісланий стан сесії: повтор без нового тексту не шлемо, щоб не затерти message

  // Не чекаємо на відповідь і ковтаємо помилки: вимкнена лампа не має заважати агентові
  const send = (sessionID, state, message) => {
    if (!sessionID || children.has(sessionID)) return
    if (message === undefined && shown.get(sessionID) === state) return
    shown.set(sessionID, state)
    const body = { state, agent_id: "opencode-" + String(sessionID).slice(-8), name, message: (message ?? "").slice(0, 100) }
    if (tasks.has(sessionID)) body.task = tasks.get(sessionID)
    const request = fetch(url, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(2000),
    }).catch(() => {}).finally(() => pending.delete(request))
    pending.add(request)
  }

  return {
    // Ці три хуки OpenCode виконує синхронно з агентом: жодного await на мережу
    "chat.message": async (input, output) => {
      const text = (output?.parts ?? []).filter((p) => p.type === "text" && !p.synthetic).map((p) => p.text).join(" ")
      if (text) tasks.set(input.sessionID, text.replace(/\s+/g, " ").slice(0, 160))
      failed.delete(input.sessionID)
      asking.delete(input.sessionID)
      send(input.sessionID, "busy", "думає")
    },
    "tool.execute.before": async (input) => {
      if (!asking.has(input.sessionID)) send(input.sessionID, "busy", input.tool)
    },
    "experimental.session.compacting": async (input) => {
      send(input.sessionID, "busy", "стискає контекст")
    },

    dispose: async () => {
      await Promise.allSettled([...pending])
    },

    event: async ({ event }) => {
      const p = event.properties ?? {}
      const id = p.sessionID
      switch (event.type) {
        case "session.created":
          if (p.info?.parentID) children.add(id)
          break
        case "session.deleted":
          send(id, "idle")
          tasks.delete(id); failed.delete(id); asking.delete(id); children.delete(id); shown.delete(id)
          break
        case "permission.asked":
          asking.add(id)
          send(id, "waiting", "Дозвіл — " + (p.permission ?? ""))
          break
        case "question.asked":
          asking.add(id)
          send(id, "waiting", p.questions?.[0]?.question ?? "питання")
          break
        case "permission.replied":
        case "question.replied":
        case "question.rejected":
          asking.delete(id)
          send(id, "busy")
          break
        case "session.error": {
          const kind = p.error?.name
          if (kind === "ContextOverflowError") break   // далі зазвичай іде автостискання, хід триває
          failed.add(id)
          asking.delete(id)
          if (kind === "MessageAbortedError") send(id, "idle")
          else send(id, "error", "Помилка: " + (p.error?.data?.message ?? kind ?? "невідома"))
          break
        }
        case "session.status":
          if (p.status?.type === "idle") {
            asking.delete(id)
            if (!failed.has(id)) send(id, "done")
          } else if (!asking.has(id)) {
            send(id, "busy", p.status?.type === "retry" ? p.status.message : undefined)
          }
          break
      }
    },
  }
}
