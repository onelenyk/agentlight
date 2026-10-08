"""Перед збіркою стискає src/page.html у src/page_gz.h: сторінка лампи їде у прошивці у gzip (12 КБ -> ~4 КБ).

Підключено в platformio.ini як extra_scripts; page_gz.h генерується і в git не потрапляє.
"""
import gzip
import pathlib

Import("env")  # noqa: F821 — надає PlatformIO

src = pathlib.Path(env.subst("$PROJECT_SRC_DIR"))  # noqa: F821
data = gzip.compress((src / "page.html").read_bytes(), 9, mtime=0)
rows = [", ".join(str(b) for b in data[i:i + 24]) for i in range(0, len(data), 24)]
text = ("// Згенеровано tools/embed_page.py з page.html — не редагувати.\n#pragma once\n#include <Arduino.h>\n\n"
        f"const size_t PAGE_GZ_LEN = {len(data)};\nconst uint8_t PAGE_GZ[] PROGMEM = {{\n  " + ",\n  ".join(rows) + "\n};\n")
out = src / "page_gz.h"
if not out.exists() or out.read_text() != text:
    out.write_text(text)
