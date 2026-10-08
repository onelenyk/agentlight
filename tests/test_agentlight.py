"""Тести всього, що з'єднує агентів із лампою і не потребує самої лампи:

  HookParseTests   — розбір подій агентів (той самий C++-код, що в прошивці) на зразках із hook_cases.json
  HookFileTests    — файли hooks/*.json: форма, яку приймає кожен агент, і адреси, які розуміє лампа
  InstallerTests   — hooks/install.sh проти імітації лампи в порожній домашній теці
  EndToEndTests    — встановлений хук, запущений як його запустив би агент, доходить до лампи

Запуск:  python3 -m unittest discover -s tests -v
Потрібні: python3, компілятор C++ (c++), curl; для одного тесту — node.
"""
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import unittest

from mock_lamp import HOOKS, MockLamp, parse_hook

TESTS = pathlib.Path(__file__).resolve().parent
STATES = {"busy", "waiting", "done", "error", "idle", "stop", "notification"}
# агент у команді встановлювача -> (назва в адресі хука, файл налаштувань у домашній теці)
AGENTS = {
    "claude": ("claude", ".claude/settings.json"),
    "codex": ("codex", ".codex/hooks.json"),
    "gemini": ("gemini", ".gemini/settings.json"),
    "qwen": ("qwen", ".qwen/settings.json"),
    "cursor": ("cursor", ".cursor/hooks.json"),
    "antigravity": ("agy", ".gemini/config/hooks.json"),
    "copilot": ("copilot", ".copilot/hooks/agentlight.json"),
}


def commands(node):
    """Усі команди хуків у файлі налаштувань, хоч як глибоко вони лежать."""
    if isinstance(node, dict):
        for key, value in node.items():
            if key in ("command", "bash") and isinstance(value, str):
                yield value
            else:
                yield from commands(value)
    elif isinstance(node, list):
        for item in node:
            yield from commands(item)


class HookParseTests(unittest.TestCase):
    def test_cases(self):
        for case in json.loads((TESTS / "hook_cases.json").read_text(encoding="utf-8")):
            with self.subTest(case["name"]):
                body = case["raw"] if "raw" in case else json.dumps(case["body"], ensure_ascii=False)
                body = body.replace("REPEAT_X_9000", "x" * 9000).encode()
                if "truncate" in case:
                    self.assertGreater(len(body), case["truncate"], "зразок має бути довшим за обрізання")
                    body = body[:case["truncate"]]
                got = parse_hook(case["tool"], case["state"], body)
                if case.get("skip"):
                    self.assertEqual(got, {"skip": True})
                    continue
                self.assertNotIn("skip", got)
                for key, want in case["expect"].items():
                    self.assertEqual(got[key], want, key)

    def test_truncation_anywhere_never_crashes(self):
        body = json.dumps({"session_id": "t1aaaaaa", "cwd": "/a/демо", "hook_event_name": "PostToolUse", "tool_name": "Bash",
                           "tool_input": {"command": "echo \"привіт\"\n", "description": "Друк \\ тест"}, "error": {"message": "x"}},
                          ensure_ascii=False).encode()
        for cut in range(len(body) + 1):
            parse_hook("claude", "stop", body[:cut])      # досить того, що розбір не падає


class HookFileTests(unittest.TestCase):
    def load(self, name):
        return json.loads((HOOKS / f"{name}.json").read_text(encoding="utf-8"))

    def test_every_command_posts_to_its_own_hook_address(self):
        for agent, (tool, _) in AGENTS.items():
            found = list(commands(self.load(agent)))
            self.assertTrue(found, agent)
            for command in found:
                with self.subTest(agent=agent, command=command[-60:]):
                    self.assertIn("head -c 4000", command)                      # лампа не приймає великих тіл
                    self.assertIn("Content-Type: application/json", command)    # інакше прошивка не побачить тіла
                    self.assertIn("-m 2", command)
                    self.assertIn(">/dev/null 2>&1 &", command)                 # запит у фоні: агент не чекає
                    address = command.split("http://agentlight.local/hook/")[1].split()[0]
                    self.assertEqual(address.split("/")[0], tool)
                    self.assertIn(address.split("/")[1], STATES)

    def test_agents_that_read_stdout_get_json(self):
        for agent in ("gemini", "antigravity"):
            for command in commands(self.load(agent)):
                self.assertRegex(command, r"& echo '\{.*\}'$", agent)
        for agent in ("claude", "codex", "qwen", "cursor", "copilot"):
            for command in commands(self.load(agent)):
                self.assertTrue(command.endswith("&"), agent)

    def test_claude_style_shape(self):
        for agent in ("claude", "codex", "qwen", "gemini"):
            for event, entries in self.load(agent)["hooks"].items():
                for entry in entries:
                    with self.subTest(agent=agent, event=event):
                        self.assertIsInstance(entry["hooks"], list)
                        self.assertEqual(entry["hooks"][0]["type"], "command")

    def test_claude_covers_all_five_states(self):
        states = {c.split("/hook/claude/")[1].split()[0] for c in commands(self.load("claude"))}
        self.assertTrue({"busy", "waiting", "done", "error", "idle"} <= states)
        self.assertEqual(self.load("claude")["hooks"]["StopFailure"][0]["hooks"][0]["command"].split("/hook/claude/")[1].split()[0], "error")

    def test_antigravity_shape(self):
        # Antigravity IDE відкидає весь файл, якщо подія без інструмента загорнута в {"hooks": [...]}:
        # для PreInvocation і Stop обробник лежить напряму, для подій інструментів — у {matcher, hooks}.
        hook = self.load("antigravity")["agentlight"]
        for event in ("PreInvocation", "Stop"):
            for entry in hook[event]:
                self.assertIn("command", entry, event)
                self.assertNotIn("hooks", entry, event)
        for entry in hook["PostToolUse"]:
            self.assertIn("matcher", entry)
            self.assertIn("command", entry["hooks"][0])
        self.assertIn('{"decision":', hook["Stop"][0]["command"])               # Stop мусить повернути рішення
        self.assertNotIn('"continue"', hook["Stop"][0]["command"])              # ...і не змусити агента працювати далі
        self.assertNotIn("PreToolUse", hook)                                    # він вирішує дозволи: не чіпаємо

    def test_cursor_and_copilot_shape(self):
        for agent, key in (("cursor", "command"), ("copilot", "bash")):
            data = self.load(agent)
            self.assertEqual(data["version"], 1)
            for entries in data["hooks"].values():
                self.assertIn(key, entries[0])

    def test_installer_knows_every_agent(self):
        script = (HOOKS / "install.sh").read_text(encoding="utf-8")
        for agent, (_, target) in AGENTS.items():
            self.assertIn(f"{agent})", script)
            self.assertIn(target, script)
        self.assertIn("opencode)", script)
        self.assertTrue((HOOKS / "opencode.js").is_file())

    def test_device_page_lists_every_agent(self):
        page = (HOOKS.parent / "firmware" / "src" / "page.html").read_text(encoding="utf-8")
        for agent in list(AGENTS) + ["opencode"]:
            self.assertIn(f"\n  {agent}:{{name:", page)


class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lamp = MockLamp().__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.lamp.__exit__()

    def setUp(self):
        self.home = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-home-"))
        self.addCleanup(shutil.rmtree, self.home, True)

    def install(self, agent, path=None):
        env = dict(os.environ, HOME=str(self.home))
        if path is not None:
            env["PATH"] = path
        return subprocess.run(f"curl -4 -fsS {self.lamp.url}/install.sh | sh -s -- {agent}", shell=True, env=env,
                              capture_output=True, text=True)

    def read(self, relative):
        return json.loads((self.home / relative).read_text(encoding="utf-8"))

    def limited_path(self, *tools):
        """Тека лише з потрібними встановлювачу програмами: щоб перевірити роботу без python3 чи node."""
        bin_dir = self.home / "bin"
        bin_dir.mkdir()
        for tool in ("sh", "curl", "mktemp", "cp", "mkdir", "date", "dirname", "rm", "cat") + tools:
            os.symlink(shutil.which(tool), bin_dir / tool)
        return str(bin_dir)

    def test_fresh_install_of_every_agent(self):
        for agent, (tool, target) in AGENTS.items():
            with self.subTest(agent):
                result = self.install(agent)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.read(target), json.loads((HOOKS / f"{agent}.json").read_text(encoding="utf-8")))
        result = self.install("opencode")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.home / ".config/opencode/plugins/agentlight.js").read_bytes(), (HOOKS / "opencode.js").read_bytes())

    def test_merge_keeps_foreign_hooks_and_settings(self):
        target = self.home / ".claude/settings.json"
        target.parent.mkdir(parents=True)
        mine = {"type": "command", "command": "echo mine"}
        target.write_text(json.dumps({"model": "opus", "hooks": {"Stop": [{"hooks": [mine]}], "SessionStart": [{"hooks": [mine]}]}}))
        self.assertEqual(self.install("claude").returncode, 0)
        data = self.read(".claude/settings.json")
        self.assertEqual(data["model"], "opus")
        self.assertEqual(data["hooks"]["SessionStart"], [{"hooks": [mine]}])
        self.assertEqual(data["hooks"]["Stop"][0], {"hooks": [mine]})
        self.assertIn("/hook/claude/done", data["hooks"]["Stop"][1]["hooks"][0]["command"])
        self.assertEqual(len(list(self.home.glob(".claude/settings.json.bak-agentlight-*"))), 1)

    def test_reinstall_replaces_own_hooks_including_old_script_ones(self):
        target = self.home / ".claude/settings.json"
        target.parent.mkdir(parents=True)
        old = {"hooks": [{"type": "command", "command": '"$HOME/.claude/hooks/agentlight.sh" done'}]}
        target.write_text(json.dumps({"hooks": {"Stop": [old]}}))
        for _ in range(3):
            self.assertEqual(self.install("claude").returncode, 0)
            time.sleep(1.1)                 # резервні копії названі за секундами
        data = self.read(".claude/settings.json")
        self.assertEqual(data["hooks"], json.loads((HOOKS / "claude.json").read_text(encoding="utf-8"))["hooks"])

    def test_antigravity_keeps_other_named_hooks(self):
        target = self.home / ".gemini/config/hooks.json"
        target.parent.mkdir(parents=True)
        target.write_text(json.dumps({"my-linter": {"PostToolUse": []}, "agentlight": {"old": True}}))
        self.assertEqual(self.install("antigravity").returncode, 0)
        data = self.read(".gemini/config/hooks.json")
        self.assertEqual(data["my-linter"], {"PostToolUse": []})
        self.assertIn("PreInvocation", data["agentlight"])
        self.assertNotIn("old", data["agentlight"])

    def test_cursor_merge_keeps_version_and_foreign_hook(self):
        target = self.home / ".cursor/hooks.json"
        target.parent.mkdir(parents=True)
        target.write_text(json.dumps({"version": 1, "hooks": {"stop": [{"command": "echo mine"}]}}))
        self.assertEqual(self.install("cursor").returncode, 0)
        data = self.read(".cursor/hooks.json")
        self.assertEqual(data["hooks"]["stop"][0], {"command": "echo mine"})
        self.assertEqual(len(data["hooks"]["stop"]), 2)

    @unittest.skipUnless(shutil.which("node"), "потрібен node")
    def test_merge_with_node_when_there_is_no_python(self):
        target = self.home / ".codex/hooks.json"
        target.parent.mkdir(parents=True)
        other = {"hooks": [{"type": "command", "command": "echo other"}]}
        target.write_text(json.dumps({"hooks": {"Stop": [other]}}))
        result = self.install("codex", self.limited_path("node"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.read(".codex/hooks.json")["hooks"]["Stop"][0], other)
        self.assertEqual(len(self.read(".codex/hooks.json")["hooks"]["Stop"]), 2)

    def test_without_python_and_node_existing_settings_stay_untouched(self):
        target = self.home / ".claude/settings.json"
        target.parent.mkdir(parents=True)
        target.write_text('{"model": "opus"}')
        result = self.install("claude", self.limited_path())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("python3", result.stderr)
        self.assertEqual(target.read_text(), '{"model": "opus"}')

    def test_broken_settings_file_is_not_overwritten(self):
        target = self.home / ".claude/settings.json"
        target.parent.mkdir(parents=True)
        target.write_text("{ не JSON")
        self.assertNotEqual(self.install("claude").returncode, 0)
        self.assertEqual(target.read_text(), "{ не JSON")

    def test_unknown_agent(self):
        result = self.install("emacs")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(self.home.iterdir()), [])


class EndToEndTests(unittest.TestCase):
    """Встановлює хуки з імітації лампи й запускає їх так, як це зробив би агент: подія на stdin."""

    def run_hook(self, lamp, command, event):
        # У файлах стоїть agentlight.local; у тесті адресу лампи підміняємо на імітацію
        command = command.replace("agentlight.local", lamp.host)
        result = subprocess.run(["sh", "-c", command], input=json.dumps(event), capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        return result.stdout

    def wait_for(self, lamp, count):
        deadline = time.time() + 5
        while len(lamp.events) < count and time.time() < deadline:
            time.sleep(0.05)
        self.assertEqual(len(lamp.events), count)
        return lamp.events[-1]

    def test_claude_session_from_prompt_to_end(self):
        with MockLamp() as lamp:
            hooks = json.loads((HOOKS / "claude.json").read_text(encoding="utf-8"))["hooks"]
            base = {"session_id": "abcdef12-3456", "cwd": "/home/u/мій проєкт"}
            steps = [
                ("UserPromptSubmit", {"prompt": "полагодь тест"}, "busy", "думає"),
                ("PostToolUse", {"tool_name": "Bash", "tool_input": {"command": "make", "description": "Run build"}}, "busy", "Bash: Run build"),
                ("PermissionRequest", {"tool_name": "Edit", "tool_input": {"file_path": "/x/a.c"}}, "waiting", "Дозвіл — Edit: a.c"),
                ("Stop", {}, "done", ""),
                ("StopFailure", {"error": "overloaded"}, "error", "Помилка: overloaded"),
            ]
            for count, (event, extra, state, message) in enumerate(steps, 1):
                out = self.run_hook(lamp, hooks[event][0]["hooks"][0]["command"], dict(base, hook_event_name=event, **extra))
                self.assertEqual(out, "", "хук Claude Code не має нічого друкувати")
                got = self.wait_for(lamp, count)
                self.assertEqual((got["state"], got["message"], got["agent_id"], got["name"]),
                                 (state, message, "claude-abcdef12", "мій проєкт"))
            self.assertEqual(lamp.events[0]["task"], "полагодь тест")
            self.run_hook(lamp, hooks["SessionEnd"][0]["hooks"][0]["command"], dict(base, hook_event_name="SessionEnd"))
            self.wait_for(lamp, len(steps) + 1)
            self.assertEqual(lamp.agents, {})

    def test_antigravity_hooks_print_what_the_ide_expects(self):
        with MockLamp() as lamp:
            hook = json.loads((HOOKS / "antigravity.json").read_text(encoding="utf-8"))["agentlight"]
            base = {"conversationId": "c089f74c-aaaa", "workspacePaths": ["/tmp/app"]}
            self.assertEqual(json.loads(self.run_hook(lamp, hook["PreInvocation"][0]["command"], base)), {})
            self.assertEqual(self.wait_for(lamp, 1)["state"], "busy")
            out = json.loads(self.run_hook(lamp, hook["Stop"][0]["command"], dict(base, terminationReason="model_stop", fullyIdle=True)))
            self.assertNotEqual(out["decision"], "continue")
            self.assertEqual(self.wait_for(lamp, 2)["state"], "done")

    def test_hook_does_not_block_when_the_lamp_is_gone(self):
        command = json.loads((HOOKS / "codex.json").read_text(encoding="utf-8"))["hooks"]["Stop"][0]["hooks"][0]["command"]
        started = time.time()
        subprocess.run(["sh", "-c", command.replace("agentlight.local", "127.0.0.1:9")], input="{}", text=True, timeout=10, check=True)
        self.assertLess(time.time() - started, 1.0)


if __name__ == "__main__":
    unittest.main()
