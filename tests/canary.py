"""Перевірка на справжніх агентах: чи остання версія кожного CLI досі приймає наші хуки і шле лампі те, що ми чекаємо.

Ставить найсвіжіші CLI у тимчасову теку, підключає їх встановлювачем до імітації лампи й запускає без входу в акаунт.
Якщо агент змінить формат хуків чи подій, цей скрипт упаде.

  claude — повні ходи проти імітації моделі (tests/mock_model.py): відповідь, виклик інструмента, помилка API.
           Перевіряє «працює», дію інструмента, «готово», «помилку» і «спокій» після закриття сесії.
  codex, qwen — один запит; до відповіді моделі не доходить, але хук «запит надіслано» спрацьовує раніше.

Запуск:  python3 tests/canary.py [claude] [codex] [qwen]     (потрібні node, npm, curl, компілятор C++)
Щотижня його запускає .github/workflows/canary.yml.
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

from mock_lamp import MockLamp
from mock_model import MockModel

PROMPT = "reply with ok"
# агент -> (пакет npm, програма, префікс agent_id, файл хуків у домашній теці, аргументи, додаткове середовище)
AGENTS = {
    "codex": ("@openai/codex", "codex", "codex-", ".codex/hooks.json",
              # нові хуки Codex не запускає без схвалення; у тесті джерело хуків — ми самі
              ["exec", "--skip-git-repo-check", "--dangerously-bypass-hook-trust", PROMPT],
              lambda home: {"CODEX_HOME": str(home / ".codex")}),      # інакше Codex читає справжню ~/.codex
    "qwen": ("@qwen-code/qwen-code", "qwen", "qwen-", ".qwen/settings.json",
             ["--auth-type", "openai", "-p", PROMPT],
             lambda home: {"OPENAI_API_KEY": "sk-not-a-real-key", "OPENAI_BASE_URL": "http://127.0.0.1:9/v1", "OPENAI_MODEL": "none"}),
}


def check(agent, tools: pathlib.Path):
    package, program, prefix, hooks_file, args, extra_env = AGENTS[agent]
    home = pathlib.Path(tempfile.mkdtemp(prefix=f"agentlight-{agent}-"))
    project = home / "canary-project"
    project.mkdir()
    binary = tools / "node_modules" / ".bin" / program
    version = subprocess.run([str(binary), "--version"], capture_output=True, text=True).stdout.strip().splitlines()[-1:]
    with MockLamp() as lamp:
        env = dict(os.environ, HOME=str(home), **extra_env(home))
        subprocess.run(f"curl -4 -fsS {lamp.url}/install.sh | sh -s -- {agent}", shell=True, env=env, check=True,
                       capture_output=True)
        installed = home / hooks_file
        installed.write_text(installed.read_text().replace("agentlight.local", lamp.host))   # лампа в тесті — імітація
        try:
            run = subprocess.run([str(binary), *args], cwd=project, env=env, stdin=subprocess.DEVNULL,
                                 capture_output=True, text=True, timeout=90)
            output = run.stdout + run.stderr
        except subprocess.TimeoutExpired as hung:
            output = f"(не завершився за 90 с)\n{hung.stdout or ''}{hung.stderr or ''}"
        time.sleep(2)                                                   # хуки шлють запит у фоні
        events = [e for e in lamp.events if e["agent_id"].startswith(prefix)]
    shutil.rmtree(home, ignore_errors=True)

    problems = []
    busy = [e for e in events if e["state"] == "busy"]
    if not busy:
        problems.append("лампа не отримала стану «працює»: агент не прийняв файл хуків або не запустив хук запиту")
    else:
        if busy[0]["name"] != project.name:
            problems.append(f"назва проєкту {busy[0]['name']!r}, а мала бути {project.name!r}: змінилось поле з текою")
        if busy[0]["task"] != PROMPT:
            problems.append(f"запит {busy[0]['task']!r}, а мав бути {PROMPT!r}: змінилось поле із запитом")
        if busy[0]["agent_id"] == prefix + "x":
            problems.append("у події немає ідентифікатора сесії: змінилось поле з ним")
    print(f"{agent} {' '.join(version)}: {'OK' if not problems else 'ЗЛАМАНО'}; події: "
          + json.dumps([[e['state'], e['message']] for e in events], ensure_ascii=False))
    for problem in problems:
        print("  - " + problem)
    if problems:
        print("  останні рядки виводу агента:\n    " + "\n    ".join(output.strip().splitlines()[-8:]))
    return not problems


def claude_turn(binary, env, project, prompt, extra_args=()):
    """Один хід Claude Code через потоковий інтерфейс: так сесія живе, доки ми самі її не закриємо,
    і хуки кінця ходу встигають спрацювати (у звичайному `claude -p` їх обриває вихід із програми)."""
    proc = subprocess.Popen([str(binary), "-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose",
                             *extra_args], cwd=project, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    proc.stdin.write(json.dumps({"type": "user", "message": {"role": "user", "content": prompt}}) + "\n")
    proc.stdin.flush()
    deadline = time.time() + 90
    for line in proc.stdout:
        if '"type":"result"' in line or time.time() > deadline:
            break
    time.sleep(2)                           # хуки шлють запит у фоні
    proc.stdin.close()
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()
    time.sleep(2)


def check_claude(tools: pathlib.Path):
    binary = tools / "node_modules" / ".bin" / "claude"
    version = subprocess.run([str(binary), "--version"], capture_output=True, text=True).stdout.strip()
    # (назва, запит, додаткові аргументи, очікувана послідовність [стан, повідомлення]); повідомлення None — будь-яке
    scenarios = [
        ("відповідь", "reply with ok", [],
         [["busy", "думає"], ["done", ""], ["idle", ""]]),
        ("виклик інструмента", "USE_TOOL please", ["--dangerously-skip-permissions"],
         [["busy", "думає"], ["busy", "Bash: Print canary"], ["done", ""], ["idle", ""]]),
        ("помилка API", "FAIL please", [],
         [["busy", "думає"], ["error", None], ["idle", ""]]),
    ]
    ok = True
    for name, prompt, args, want in scenarios:
        home = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-claude-"))
        project = home / "canary-project"
        project.mkdir()
        with MockLamp() as lamp, MockModel() as model:
            # чисте середовище: ні справжнього акаунта, ні справжнього API, ні налаштувань із ~/.claude
            env = {"PATH": os.environ["PATH"], "HOME": str(home), "CLAUDE_CONFIG_DIR": str(home / ".claude"),
                   "ANTHROPIC_API_KEY": "sk-ant-not-a-real-key", "ANTHROPIC_BASE_URL": model.url,
                   "DISABLE_TELEMETRY": "1", "DISABLE_AUTOUPDATER": "1", "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1"}
            subprocess.run(f"curl -4 -fsS {lamp.url}/install.sh | sh -s -- claude", shell=True, env=env, check=True,
                           capture_output=True)
            installed = home / ".claude" / "settings.json"
            installed.write_text(installed.read_text().replace("agentlight.local", lamp.host))
            claude_turn(binary, env, project, prompt, args)
            got = [[e["state"], e["message"]] for e in lamp.events]
            details = lamp.events[0] if lamp.events else {}
        shutil.rmtree(home, ignore_errors=True)
        problems = []
        same = len(got) == len(want) and all(g[0] == w[0] and w[1] in (None, g[1]) for g, w in zip(got, want))
        if not same:
            problems.append(f"послідовність станів не та: чекали {json.dumps(want, ensure_ascii=False)}")
        if details and (details["name"], details["task"]) != (project.name, prompt):
            problems.append(f"проєкт і запит: {details['name']!r}, {details['task']!r}")
        print(f"claude {version}, {name}: {'OK' if not problems else 'ЗЛАМАНО'}; події: {json.dumps(got, ensure_ascii=False)}")
        for problem in problems:
            print("  - " + problem)
        ok = ok and not problems
    return ok


if __name__ == "__main__":
    chosen = sys.argv[1:] or ["claude", *AGENTS]
    packages = ["@anthropic-ai/claude-code" if a == "claude" else AGENTS[a][0] for a in chosen]
    tools = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-cli-"))
    subprocess.run(["npm", "install", "--prefix", str(tools), "--no-audit", "--no-fund", *packages], check=True, capture_output=True)
    results = [check_claude(tools) if agent == "claude" else check(agent, tools) for agent in chosen]
    shutil.rmtree(tools, ignore_errors=True)
    sys.exit(0 if all(results) else 1)
