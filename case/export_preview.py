"""Оновлює 3D-моделі на сторінці превью (case/preview/index.html) з поточного case/build.py.

Запуск:  .venv/bin/python case/export_preview.py
Деталі пакуються у складеному положенні в base64 і вставляються в рядок `const MESHES = ...;` сторінки.
Тексти, розміри й таблиці на сторінці цей скрипт не чіпає — їх після зміни моделі треба оновити вручну.
"""
import base64
import json
import pathlib

import numpy as np

import build as B


def pack(m):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3].astype("<f4")
    t = np.asarray(mesh.tri_verts)
    assert len(v) < 65536, "сторінка зберігає індекси у 16 бітах"
    return {"v": base64.b64encode(v.tobytes()).decode(), "i": base64.b64encode(t.astype("<u2").tobytes()).decode(),
            "tris": int(len(t))}


if __name__ == "__main__":
    data = {name: pack(m) for name, m in B.parts().items()}
    data["board"] = pack(B.dummy_board())
    data["touch"] = pack(B.dummy_touch())
    x0 = B.HOLDER_R0 + B.LED_LIP + 0.15
    data["dims"] = {"zLed": B.Z_LED, "pcbR": x0 + B.LED_PCB_T / 2, "pcbT": B.LED_PCB_T, "pcbD": B.LED_PCB_D,
                    "chipR": x0 + B.LED_PCB_T + 0.75, "twist": 24, "h": B.HEIGHT}

    page = pathlib.Path(__file__).parent / "preview" / "index.html"
    lines = page.read_text(encoding="utf-8").split("\n")
    at = [i for i, line in enumerate(lines) if line.startswith("const MESHES = ")]
    assert len(at) == 1, "у сторінці має бути рівно один рядок `const MESHES = ...;`"
    lines[at[0]] = "const MESHES = " + json.dumps(data, separators=(",", ":")) + ";"
    page.write_text("\n".join(lines), encoding="utf-8")
    print({k: v["tris"] for k, v in data.items() if "tris" in v})
