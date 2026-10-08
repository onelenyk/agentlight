"""Перевірка на справжніх агентах: чи остання версія кожного CLI досі приймає наші хуки і шле лампі те, що ми чекаємо.

Ставить найсвіжіші Codex CLI та Qwen Code у тимчасову теку, підключає їх встановлювачем до імітації лампи
й запускає по одному запиту без входу в акаунт. До відповіді моделі справа не доходить, але хук «запит надіслано»
спрацьовує раніше — його й перевіряємо. Якщо агент змінить формат хуків чи подій, цей скрипт упаде.

Запуск:  python3 tests/canary.py [codex] [qwen]     (потрібні node, npm, curl, компілятор C++)
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


if __name__ == "__main__":
    chosen = sys.argv[1:] or list(AGENTS)
    tools = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-cli-"))
    subprocess.run(["npm", "install", "--prefix", str(tools), "--no-audit", "--no-fund", *[AGENTS[a][0] for a in chosen]],
                   check=True, capture_output=True)
    results = [check(agent, tools) for agent in chosen]
    shutil.rmtree(tools, ignore_errors=True)
    sys.exit(0 if all(results) else 1)
