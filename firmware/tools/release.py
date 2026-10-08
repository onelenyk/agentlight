"""Готує прошивку до публікації: підписує її і пише файл «яка версія остання».

  python3 firmware/tools/release.py --key ключ.pem --out site/firmware [--bin firmware.bin]

У теці --out з'являються firmware-<версія>.bin і latest.json: версія, розмір, SHA-256 і підпис ECDSA P-256 (base64).
Сторінка лампи читає latest.json, завантажує файл і передає лампі разом із підписом; лампа перевіряє підпис
відкритим ключем firmware/update_public_key.pem, вшитим у прошивку. Версія береться з FW_VERSION в src/app.h.
Потрібен openssl.
"""
import argparse
import base64
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import subprocess

FIRMWARE = pathlib.Path(__file__).resolve().parent.parent


def source_version():
    return re.search(r'#define FW_VERSION "([^"]+)"', (FIRMWARE / "src" / "app.h").read_text(encoding="utf-8")).group(1)


def sign(binary: pathlib.Path, key: pathlib.Path) -> bytes:
    return subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(key), str(binary)], check=True, capture_output=True).stdout


def release(binary: pathlib.Path, key: pathlib.Path, out: pathlib.Path, version=None):
    version = version or source_version()
    data = binary.read_bytes()
    if version.encode() not in data:
        raise SystemExit(f"у {binary} немає рядка версії {version}: зібрано з іншого коду?")
    out.mkdir(parents=True, exist_ok=True)
    name = f"firmware-{version}.bin"
    shutil.copyfile(binary, out / name)
    manifest = {"version": version, "file": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                "signature": base64.b64encode(sign(binary, key)).decode(),
                "published": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    (out / "latest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--bin", type=pathlib.Path, default=FIRMWARE / ".pio" / "build" / "c3_supermini" / "firmware.bin")
    parser.add_argument("--key", type=pathlib.Path, required=True, help="закритий ключ підпису (PEM)")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(json.dumps(release(args.bin, args.key, args.out), indent=2))
