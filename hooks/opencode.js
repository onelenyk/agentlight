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
// «Дозволити» (експериментально). Якщо є ~/.agentlight/allow.conf (його пише встановлювач зі словом allow),
// то на permission.asked плагін ще й питає лампу. Питання в терміналі лишається на екрані: хто відповів
// першим — термінал чи дотик до лампи — того відповідь і діє. Відповідь лампи приймається лише з правильним підписом.
//
// У файлі не має бути інших експортів, крім функції плагіна: OpenCode вважає плагіном кожен експорт.
import { createHmac, randomBytes } from "node:crypto"
import { lookup } from "node:dns/promises"
import { readFileSync } from "node:fs"
import { homedir } from "node:os"

export const AgentLight = async ({ directory, worktree, client }) => {
  // Адресу лампи шукаємо самі й лише в IPv4: звичайний fetch за іменем *.local на macOS спершу 5 секунд чекає
  // на IPv6-відповідь, якої лампа не дає, і не вкладається в наш час очікування. Знайдену адресу пам'ятаємо.
  const known = new Map()
  const lampFetch = async (host, path, init, timeoutMs = 2000) => {
    let address = known.get(host)
    if (!address) {
      const [name, port] = host.split(":")
      const ip = /^[\d.]+$/.test(name) ? name : (await lookup(name, { family: 4 })).address
      address = port ? `${ip}:${port}` : ip
      known.set(host, address)
    }
    try {
      return await fetch(`http://${address}${path}`, { ...init, signal: AbortSignal.timeout(timeoutMs) })   // час — лише на сам запит
    } catch (error) {
      known.delete(host)                                 // лампа могла отримати нову адресу
      throw error
    }
  }
  const statusHost = process.env.AGENTLIGHT_HOST ?? "agentlight.local"
  let allow = null             // {HOST, KEY, SECRET}, якщо комп'ютер спарований для «Дозволити»
  try {
    allow = Object.fromEntries(readFileSync(homedir() + "/.agentlight/allow.conf", "utf8").split("\n").filter(Boolean).map((l) => l.split("=")))
    if (!allow.HOST || !allow.KEY || !allow.SECRET) allow = null
  } catch {}
  const lampAsks = new Map()   // id запиту OpenCode -> id запиту на лампі, щоб зняти його, коли відповіли в терміналі
  const answered = new Set()   // запити, на які в OpenCode вже відповіли: лампі про них питати пізно
  const cancelOnLamp = (lampID) => lampFetch(allow.HOST, "/api/allow/cancel", {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ id: lampID }),
  }).catch(() => {})

  // Питає лампу про дозвіл і, якщо власник відповів дотиком, відповідає OpenCode замість нього
  const askLamp = async (p) => {
    const call = (path, init) => lampFetch(allow.HOST, "/api/allow" + path, init, 3000).then((r) => r.json())
    const nonce = randomBytes(8).toString("hex")
    const started = await call("/ask", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ key: allow.KEY, nonce, agent_id: "opencode-" + String(p.sessionID).slice(-8), name,
                             tool: p.permission ?? "", detail: (p.patterns ?? []).join(" ") }),
    })
    if (!started.id) return                              // лампа відмовилась пропонувати: відповідай у терміналі
    if (answered.delete(p.id)) return cancelOnLamp(started.id)   // відповіли в терміналі, поки запит ішов до лампи
    lampAsks.set(p.id, started.id)
    for (let i = 0; i < 34 && lampAsks.has(p.id); i++) {
      await new Promise((r) => setTimeout(r, 500))
      const answer = await call("/ask?id=" + started.id)
      if (answer.state === "pending") continue
      lampAsks.delete(p.id)
      if (answer.state !== "allow" && answer.state !== "deny") return
      const want = createHmac("sha256", allow.SECRET).update(nonce + ":" + answer.state).digest("hex")
      if (answer.sig !== want) return                     // відповів хтось, хто не знає секрету: не лампа
      await client.postSessionIdPermissionsPermissionId({
        path: { id: p.sessionID, permissionID: p.id },
        body: { response: answer.state === "allow" ? "once" : "reject" },
      })
      return
    }
  }
  const cancelLampAsk = (requestID) => {
    const lampID = lampAsks.get(requestID)
    if (lampID === undefined) { answered.add(requestID); return }   // запит до лампи ще в дорозі: askLamp зніме його сам
    lampAsks.delete(requestID)
    cancelOnLamp(lampID)
  }
  const name = (worktree && worktree !== "/" ? worktree : directory).split("/").filter(Boolean).pop() ?? "opencode"
  const children = new Set()   // сесії підагентів: їхній idle — не кінець ходу, тому мовчимо про них
  const failed = new Set()     // сесії, де цей хід уже закінчився помилкою чи перериванням
  const asking = new Set()     // сесії, що чекають відповіді: busy від session.status не має затерти waiting
  const tasks = new Map()
  let queue = Promise.resolve() // черга станів до лампи
  const pending = new Set()    // запити в дорозі: dispose дочекається їх, щоб «готово» не загубилось при виході
  const shown = new Map()      // останній надісланий стан сесії: повтор без нового тексту не шлемо, щоб не затерти message

  // Не чекаємо на відповідь і ковтаємо помилки: вимкнена лампа не має заважати агентові
  const send = (sessionID, state, message) => {
    if (!sessionID || children.has(sessionID)) return
    if (message === undefined && shown.get(sessionID) === state) return
    shown.set(sessionID, state)
    const body = { state, agent_id: "opencode-" + String(sessionID).slice(-8), name, message: (message ?? "").slice(0, 100) }
    if (tasks.has(sessionID)) body.task = tasks.get(sessionID)
    // Стани йдуть по одному, в порядку появи: інакше «готово» могло б випередити «працює» й лампа показала б не те
    const request = (queue = queue.then(() => lampFetch(statusHost, "/api/status", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(body),
    }).catch(() => {}))).finally(() => pending.delete(request))
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
          if (allow && !children.has(id)) askLamp(p).catch(() => {})   // не чекаємо: питання в терміналі вже на екрані
          break
        case "question.asked":
          asking.add(id)
          send(id, "waiting", p.questions?.[0]?.question ?? "питання")
          break
        case "permission.replied":
          if (allow) cancelLampAsk(p.requestID)
          asking.delete(id)
          send(id, "busy")
          break
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
