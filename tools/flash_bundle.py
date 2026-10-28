#!/usr/bin/env python3
"""Verify every image before flashing. Does not erase NVS."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def image_arguments(root, manifest):
    result = []
    for offset, item in manifest["files"].items():
        path = (root / item["file"]).resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError("镜像路径越界")
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError(f"校验失败：{path.name}")
        result.extend([offset, str(path)])
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    images = image_arguments(root, manifest)
    settings = manifest["flash_settings"]
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port, "write-flash"]
    for name in ("flash_mode", "flash_freq", "flash_size"):
        if name in settings:
            command += ["--" + name.replace("_", "-"), settings[name]]
    subprocess.run(command + images, check=True)


if __name__ == "__main__":
    main()

