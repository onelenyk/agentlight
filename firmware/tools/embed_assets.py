"""Перед збіркою пакує в src/assets.h усе, що лампа роздає сама:

  src/page.html          -> сторінка лампи (gzip, 20 КБ -> ~8 КБ)
  ../hooks/*.json, *.js  -> готові хуки агентів, GET /setup/<файл> (gzip)
  ../hooks/install.sh    -> встановлювач, GET /install.sh (без стиснення: лампа дописує до нього свою адресу)
  update_public_key.pem  -> відкритий ключ, яким лампа перевіряє підпис прошивки при оновленні

Підключено в platformio.ini як extra_scripts; assets.h генерується і в git не потрапляє.
"""
import gzip
import pathlib

Import("env")  # noqa: F821 — надає PlatformIO

src = pathlib.Path(env.subst("$PROJECT_SRC_DIR"))  # noqa: F821
hooks = src.parent.parent / "hooks"
TYPES = {".json": "application/json", ".js": "text/javascript"}


def array(name, data):
    rows = [", ".join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return f"const uint8_t {name}[] PROGMEM = {{\n  " + ",\n  ".join(rows) + "\n};\n"


text = "// Згенеровано tools/embed_assets.py — не редагувати.\n#pragma once\n#include <Arduino.h>\n\n"
page = gzip.compress((src / "page.html").read_bytes(), 9, mtime=0)
text += f"const size_t PAGE_GZ_LEN = {len(page)};\n" + array("PAGE_GZ", page) + "\n"

install = (hooks / "install.sh").read_bytes()
text += f"const size_t INSTALL_SH_LEN = {len(install)};\n" + array("INSTALL_SH", install) + "\n"

key = (src.parent / "update_public_key.pem").read_text().strip()
text += "\nconst char UPDATE_PUBLIC_KEY[] = \n" + "\n".join(f'  "{line}\\n"' for line in key.splitlines()) + ";\n\n"

entries = []
for i, f in enumerate(sorted(p for p in hooks.iterdir() if p.suffix in TYPES)):
    data = gzip.compress(f.read_bytes(), 9, mtime=0)
    text += array(f"SETUP_{i}", data)
    entries.append(f'  {{"{f.name}", "{TYPES[f.suffix]}", SETUP_{i}, {len(data)}}},')
text += ("\nstruct SetupFile { const char* name; const char* type; const uint8_t* data; size_t len; };\n"
         "const SetupFile SETUP_FILES[] = {\n" + "\n".join(entries) + "\n};\n")

out = src / "assets.h"
if not out.exists() or out.read_text() != text:
    out.write_text(text)
