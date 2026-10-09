"""Тести всього, що з'єднує агентів із лампою і не потребує самої лампи:

  HookParseTests   — розбір подій агентів (той самий C++-код, що в прошивці) на зразках із hook_cases.json
  HookFileTests    — файли hooks/*.json: форма, яку приймає кожен агент, і адреси, які розуміє лампа
  InstallerTests   — hooks/install.sh проти імітації лампи в порожній домашній теці
  EndToEndTests    — встановлений хук, запущений як його запустив би агент, доходить до лампи
  ReleaseTests     — підпис прошивки й файл latest.json, з якого сторінка лампи бере оновлення
  LogicTests       — жести сенсора, таймер фокусу, ігри, правила «Дозволити» (tests/logic_test.cpp, код із прошивки)
  AllowTests       — «Дозволити»: спарювання встановлювачем і хук, що питає лампу й перевіряє її підпис

Запуск:  python3 -m unittest discover -s tests -v
Потрібні: python3, компілятор C++ (c++), curl, openssl; для одного тесту — node.
"""
import base64
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

from mock_lamp import HOOKS, MockLamp, parse_hook

TESTS = pathlib.Path(__file__).resolve().parent
FIRMWARE = TESTS.parent / "firmware"
sys.path.insert(0, str(FIRMWARE / "tools"))
import release  # noqa: E402 — firmware/tools/release.py
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
                    if agent == "claude" and command.endswith("/hook/claude/idle >/dev/null 2>&1"):
                        self.assertIn("-m 1", command)                          # кінець сесії: єдиний хук, що чекає на curl
                    else:
                        self.assertIn("-m 2", command)
                        self.assertIn(">/dev/null 2>&1 &", command)             # запит у фоні: агент не чекає
                    address = command.split("http://agentlight.local/hook/")[1].split()[0]
                    self.assertEqual(address.split("/")[0], tool)
                    self.assertIn(address.split("/")[1], STATES)

    def test_agents_that_read_stdout_get_json(self):
        for agent in ("gemini", "antigravity"):
            for command in commands(self.load(agent)):
                self.assertRegex(command, r"& echo '\{.*\}'$", agent)
        for agent in ("claude", "codex", "qwen", "cursor", "copilot"):
            for command in commands(self.load(agent)):
                self.assertNotIn("echo", command, agent)

    def test_claude_style_shape(self):
        for agent in ("claude", "codex", "qwen", "gemini"):
            for event, entries in self.load(agent)["hooks"].items():
                for entry in entries:
                    with self.subTest(agent=agent, event=event):
                        self.assertIsInstance(entry["hooks"], list)
                        self.assertEqual(entry["hooks"][0]["type"], "command")

    def test_session_end_hook_waits_for_its_request(self):
        # Усе, що Claude Code запустив у фоні, він обриває при виході, і «спокій» не встигав дійти до справжньої
        # лампи: сесія висіла на ній до кінця свого часу. Тому хук кінця сесії — єдиний — чекає на curl, але недовго.
        for name in ("claude", "claude-allow"):
            hooks = self.load(name)["hooks"]
            end = hooks["SessionEnd"][0]["hooks"][0]
            self.assertNotIn("async", end, name)
            self.assertFalse(end["command"].rstrip().endswith("&"), name)
            self.assertIn("-m 1 ", end["command"], name)
            self.assertTrue(hooks["Stop"][0]["hooks"][0]["async"], name)

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


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-release-"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.key = self.dir / "key.pem"
        self.public = self.dir / "public.pem"
        subprocess.run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(self.key)], check=True, capture_output=True)
        subprocess.run(["openssl", "ec", "-in", str(self.key), "-pubout", "-out", str(self.public)], check=True, capture_output=True)
        self.version = release.source_version()
        self.binary = self.dir / "firmware.bin"
        self.binary.write_bytes(os.urandom(5000) + self.version.encode() + os.urandom(5000))

    def verify(self, file, signature: bytes):
        sig = self.dir / "sig"
        sig.write_bytes(signature)
        return subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(self.public), "-signature", str(sig), str(file)],
                              capture_output=True).returncode == 0

    def test_manifest_describes_a_correctly_signed_file(self):
        manifest = release.release(self.binary, self.key, self.dir / "out")
        published = self.dir / "out" / manifest["file"]
        self.assertEqual(json.loads((self.dir / "out" / "latest.json").read_text()), manifest)
        self.assertEqual(manifest["version"], self.version)
        self.assertEqual(manifest["file"], f"firmware-{self.version}.bin")
        self.assertEqual(manifest["size"], published.stat().st_size)
        self.assertEqual(manifest["sha256"], hashlib.sha256(published.read_bytes()).hexdigest())
        signature = base64.b64decode(manifest["signature"])
        self.assertLessEqual(len(signature), 80, "лампа відводить під підпис 80 байтів")
        self.assertTrue(self.verify(published, signature))

    def test_signature_does_not_fit_a_changed_file(self):
        manifest = release.release(self.binary, self.key, self.dir / "out")
        changed = self.dir / "changed.bin"
        data = bytearray(self.binary.read_bytes())
        data[100] ^= 1
        changed.write_bytes(data)
        self.assertFalse(self.verify(changed, base64.b64decode(manifest["signature"])))

    def test_refuses_a_binary_built_from_another_version(self):
        self.binary.write_bytes(os.urandom(4000))
        with self.assertRaises(SystemExit):
            release.release(self.binary, self.key, self.dir / "out")

    def test_embedded_public_key_is_a_p256_key(self):
        out = subprocess.run(["openssl", "ec", "-pubin", "-in", str(FIRMWARE / "update_public_key.pem"), "-noout", "-text"],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("prime256v1", out.stdout)

    def test_no_private_key_in_the_repository(self):
        tracked = subprocess.run(["git", "ls-files"], cwd=TESTS.parent, capture_output=True, text=True, check=True).stdout.split("\n")
        for name in tracked:
            path = TESTS.parent / name
            if name and path.is_file() and path.stat().st_size < 200_000 and path.suffix not in (".stl", ".3mf", ".png", ".bin"):
                self.assertNotIn("PRIVATE KEY-----", path.read_text(encoding="utf-8", errors="ignore").replace('"PRIVATE KEY-----"', ""), name)

    def test_page_and_workflow_agree_on_where_updates_live(self):
        page = (FIRMWARE / "src" / "page.html").read_text(encoding="utf-8")
        self.assertIn("const UPDATE_URL='https://onelenyk.github.io/agentlight/firmware/'", page)
        workflow = (TESTS.parent / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        self.assertIn("--out _site/firmware", workflow)


class AllowTests(unittest.TestCase):
    """Комп'ютерний бік «Дозволити» проти імітації лампи. Головне тут — коли хук НЕ має нічого дозволяти."""
    EVENT = {"session_id": "abcdef12-3456", "cwd": "/home/u/proj", "hook_event_name": "PermissionRequest",
             "tool_name": "Bash", "tool_input": {"command": "npm test", "description": "Run tests"}}

    def setUp(self):
        self.lamp = MockLamp().__enter__()
        self.addCleanup(self.lamp.__exit__)
        self.home = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-allow-"))
        self.addCleanup(shutil.rmtree, self.home, True)
        self.env = dict(os.environ, HOME=str(self.home), AGENTLIGHT_HOST=self.lamp.host)

    def install(self, *args):
        return subprocess.run(f"curl -4 -fsS {self.lamp.url}/install.sh | sh -s -- {' '.join(args)}", shell=True, env=self.env,
                              capture_output=True, text=True)

    def hook(self, event=None):
        result = subprocess.run([str(self.home / ".agentlight" / "allow.sh"), "claude"], input=json.dumps(event or self.EVENT),
                                env=self.env, capture_output=True, text=True, timeout=40)
        self.assertEqual(result.returncode, 0)
        return result.stdout.strip()

    def paired(self):
        result = self.install("claude", "allow")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_pairing_installs_everything_and_keeps_the_secret_private(self):
        self.paired()
        conf = self.home / ".agentlight" / "allow.conf"
        values = dict(line.split("=", 1) for line in conf.read_text().split())
        self.assertEqual(values["HOST"], self.lamp.host)
        self.assertEqual(self.lamp.keys[values["KEY"]], values["SECRET"])
        self.assertEqual(len(values["SECRET"]), 32)
        self.assertEqual(conf.stat().st_mode & 0o077, 0, "секрет мають читати лише власник")
        self.assertTrue(os.access(self.home / ".agentlight" / "allow.sh", os.X_OK))
        entries = json.loads((self.home / ".claude" / "settings.json").read_text())["hooks"]["PermissionRequest"]
        self.assertEqual([("allow.sh" in e["hooks"][0]["command"], e["hooks"][0].get("async", False)) for e in entries],
                         [(False, True), (True, False)], "хук рішення має бути синхронним, сповіщення — фоновим")

    def test_rejected_or_ignored_pairing_changes_nothing(self):
        for answer in ("rejected", "expired"):
            self.lamp.pair_answer = answer
            result = self.install("claude", "allow")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((self.home / ".agentlight" / "allow.conf").exists())
            self.assertFalse((self.home / ".claude" / "settings.json").exists())

    def test_allow_is_only_for_agents_that_support_it(self):
        result = self.install("gemini", "allow")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.lamp.keys, {})

    def test_plain_install_removes_the_allow_hook(self):
        self.paired()
        self.assertEqual(self.install("claude").returncode, 0)
        entries = json.loads((self.home / ".claude" / "settings.json").read_text())["hooks"]["PermissionRequest"]
        self.assertFalse(any("allow.sh" in e["hooks"][0]["command"] for e in entries))

    def test_touch_allows_and_hold_denies(self):
        self.paired()
        self.assertEqual(json.loads(self.hook())["hookSpecificOutput"]["decision"], {"behavior": "allow"})
        self.assertIn('"command": "npm test"', self.lamp.asks[0]["raw"])
        self.assertEqual(self.lamp.asks[0]["agent_id"], "claude-abcdef12")
        self.lamp.allow_answer = "deny"
        decision = json.loads(self.hook())["hookSpecificOutput"]
        self.assertEqual((decision["hookEventName"], decision["decision"]["behavior"]), ("PermissionRequest", "deny"))
        self.assertNotEqual(self.lamp.asks[0]["nonce"], self.lamp.asks[1]["nonce"], "кожен запит має свій одноразовий номер")

    def test_no_decision_when_the_lamp_does_not_decide(self):
        self.paired()
        for reason in ("disabled", "dangerous", "category", "busy", "too long"):
            self.lamp.refuse = reason
            self.assertEqual(self.hook(), "", reason)
        self.lamp.refuse = None
        self.lamp.allow_answer = "expired"
        self.assertEqual(self.hook(), "")

    def test_forged_answer_is_ignored(self):
        self.paired()
        self.lamp.forge_signature = True            # «дозволити» від когось, хто не знає секрету
        self.assertEqual(self.hook(), "")
        self.lamp.allow_answer = "deny"
        self.assertEqual(self.hook(), "")

    def test_without_pairing_or_lamp_the_hook_stays_silent(self):
        self.paired()
        (self.home / ".agentlight" / "allow.conf").write_text("HOST=127.0.0.1:9\nKEY=aabbccdd\nSECRET=" + "0" * 32 + "\n")
        started = time.time()
        self.assertEqual(self.hook(), "")           # лампи немає
        self.assertLess(time.time() - started, 5)
        (self.home / ".agentlight" / "allow.conf").unlink()
        self.assertEqual(self.hook(), "")           # комп'ютер не спарований

    def test_release_builds_cannot_be_touched_over_the_network(self):
        # Маршрут «дотик запитом» існує лише для перевірки на столі. У звичайній збірці його не має бути,
        # інакше будь-хто в мережі дозволяв би дії замість власника.
        source = (FIRMWARE / "src" / "allow.cpp").read_text(encoding="utf-8")
        before, _, guarded = source.partition("#ifdef ALLOW_TEST_TOUCH")
        self.assertNotIn("/api/allow/touch", before)
        self.assertIn("/api/allow/touch", guarded.partition("#endif")[0])
        for config in [FIRMWARE / "platformio.ini", *(TESTS.parent / ".github" / "workflows").glob("*.yml")]:
            self.assertNotIn("ALLOW_TEST_TOUCH", config.read_text(encoding="utf-8"), config.name)

    def test_combined_claude_file_is_the_plain_one_plus_the_decision_hook(self):
        plain = json.loads((HOOKS / "claude.json").read_text())
        combined = json.loads((HOOKS / "claude-allow.json").read_text())
        extra = combined["hooks"]["PermissionRequest"].pop()
        self.assertEqual(combined, plain)
        self.assertEqual(extra["hooks"][0]["command"], '"$HOME/.agentlight/allow.sh" claude')
        self.assertNotIn("async", extra["hooks"][0])
        self.assertGreater(extra["hooks"][0]["timeout"], 17, "хук має пережити 15 секунд очікування дотику")


class LogicTests(unittest.TestCase):
    def test_gestures_and_focus_timer(self):
        out = pathlib.Path(tempfile.mkdtemp(prefix="agentlight-logic-")) / "logic_test"
        self.addCleanup(shutil.rmtree, out.parent, True)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(out), str(TESTS / "logic_test.cpp")], check=True)
        result = subprocess.run([str(out)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_page_knows_every_mode_and_action_of_the_firmware(self):
        settings = (FIRMWARE / "src" / "settings.cpp").read_text(encoding="utf-8")
        page = (FIRMWARE / "src" / "page.html").read_text(encoding="utf-8")
        names = lambda array: re.findall(r'"(\w+)"', re.search(array + r"\[\w+\] = \{(.*?)\};", settings, re.S).group(1))
        for mode in names("MODE_NAMES"):
            self.assertIn(f'data-mode="{mode}"', page)
            self.assertIn(f'id="p_{mode}"', page)
        labels = re.search(r"act:\{(.*?)\}", page, re.S).group(1)
        for action in names("ACTION_NAMES"):
            self.assertRegex(labels, rf"\b{action}:'", action)
        animations = re.search(r"anim:\{(.*?)\}", page, re.S).group(1)
        for anim in names("ANIM_NAMES"):
            self.assertRegex(animations, rf"\b{anim}:'", anim)


if __name__ == "__main__":
    unittest.main()
