#!/usr/bin/env python3
"""Portable, shell-free project commands. Requires Python 3.10+."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
IDF_VERSION = "v5.5.3"
IDF_COMMIT = "2c211b236707889e8400c4dc5644dd5c4ee071e0"


def run(args, *, env=None, cwd=ROOT):
    return subprocess.run([str(a) for a in args], cwd=cwd, env=env, check=True)


def idf_path():
    return Path(os.environ.get("IDF_PATH", ROOT / ".tools" / "esp-idf")).resolve()


def tools_path():
    return Path(os.environ.get("IDF_TOOLS_PATH", ROOT / ".tools" / "idf-tools")).resolve()


def check_idf(path):
    if not (path / "tools" / "idf.py").is_file():
        raise RuntimeError("ESP-IDF 未安装。运行 tools/project.py setup，或设置 IDF_PATH。")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=path, text=True).strip()
    if commit != IDF_COMMIT:
        raise RuntimeError(f"需要 {IDF_VERSION} ({IDF_COMMIT[:12]})，实际为 {commit[:12]}。请使用独立 SDK。")


def idf_environment():
    path = idf_path()
    check_idf(path)
    env = dict(os.environ, IDF_PATH=str(path), IDF_TOOLS_PATH=str(tools_path()))
    if "IDF_PYTHON_ENV_PATH" not in env:
        environments = list((tools_path() / "python_env").glob("idf5.5_py*_env"))
        if len(environments) == 1:
            env["IDF_PYTHON_ENV_PATH"] = str(environments[0])
    # idf_tools selects the correct virtualenv and platform toolchain, including Windows .exe files.
    raw = subprocess.check_output(
        [sys.executable, str(path / "tools" / "idf_tools.py"), "export", "--format", "key-value"],
        env=env, text=True,
    )
    for line in raw.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if not key.replace("_", "").isalnum():
            continue
        for name in ("PATH", "IDF_PATH", "IDF_TOOLS_PATH"):
            value = value.replace("${" + name + "}", env.get(name, "")).replace("$" + name, env.get(name, ""))
            value = value.replace("%" + name + "%", env.get(name, ""))
        env[key] = value.strip('"')
    if "IDF_PYTHON_ENV_PATH" not in env:
        raise RuntimeError("IDF Python 环境缺失，请先运行 setup。")
    return path, env


def idf_python(env):
    return Path(env["IDF_PYTHON_ENV_PATH"]) / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def idf(args):
    path, env = idf_environment()
    run([idf_python(env), path / "tools/idf.py", "-B", ROOT / "build", *args], env=env)


def setup():
    if sys.version_info >= (3, 14):
        raise RuntimeError("ESP-IDF 5.5.3 请使用 Python 3.10–3.13；例如 python3.13 tools/project.py setup。")
    path = idf_path()
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "-c", "core.longpaths=true", "clone", "--branch", IDF_VERSION, "--depth", "1", "--recursive", "--shallow-submodules",
             "https://github.com/espressif/esp-idf.git", path])
    check_idf(path)
    run(["git", "-c", "core.longpaths=true", "submodule", "update", "--init", "--recursive", "--depth", "1"], cwd=path)
    env = dict(os.environ, IDF_PATH=str(path), IDF_TOOLS_PATH=str(tools_path()))
    run([sys.executable, path / "tools/idf_tools.py", "install", "--targets=esp32s3"], env=env)
    run([sys.executable, path / "tools/idf_tools.py", "install", "cmake", "ninja"], env=env)
    run([sys.executable, path / "tools/idf_tools.py", "install-python-env"], env=env)
    print("环境准备完成。运行 tools/project.py build。")


def choose_port(explicit, env):
    if explicit:
        return explicit
    code = "import json; from serial.tools import list_ports; print(json.dumps([p.device for p in list_ports.comports() if p.vid is not None]))"
    ports = json.loads(subprocess.check_output([idf_python(env), "-c", code], env=env, text=True))
    if len(ports) != 1:
        raise RuntimeError(f"USB 串口候选：{ports or '无'}。请使用 --port 显式选择。")
    return ports[0]


def package(build_dir=ROOT / "build", output=ROOT / "dist"):
    source = build_dir / "flasher_args.json"
    if not source.is_file():
        raise RuntimeError("缺少 build/flasher_args.json，请先 build。")
    args = json.loads(source.read_text(encoding="utf-8"))
    description = json.loads((build_dir / "project_description.json").read_text(encoding="utf-8"))
    version = description.get("project_version", "0.1.0")
    folder = output / f"camera-rc-{version}"
    folder.mkdir(parents=True, exist_ok=True)
    files = {}
    for offset, name in args["flash_files"].items():
        src = (build_dir / name).resolve()
        if not src.is_relative_to(build_dir.resolve()):
            raise RuntimeError("刷写清单包含构建目录之外的文件。")
        target = folder / f"{offset}-{src.name}"
        shutil.copy2(src, target)
        files[offset] = {"file": target.name, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()}
    manifest = {"version": version, "chip": "esp32s3", "idf": IDF_VERSION,
                "protocol_status": "upstream-implemented-hardware-unverified", "flash_settings": args.get("flash_settings", {}), "files": files}
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(ROOT / "tools/flash_bundle.py", folder / "flash_bundle.py")
    shutil.copytree(ROOT / "LICENSES", folder / "LICENSES", dirs_exist_ok=True)
    for notice in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / notice, folder / notice)
    (folder / "README.txt").write_text(
        "ESP32-S3-Touch-AMOLED-1.43C / camera-rc\n"
        "已实现 furble 富士 Basic/Secure 协议；X-E5 实机兼容性尚待验收。\n"
        "安装 esptool 5.x：python -m pip install 'esptool>=5,<6'\n"
        "刷写：python flash_bundle.py --port <串口>\n"
        "普通刷写保留 NVS。不要使用 erase-all。\n", encoding="utf-8")
    archive = shutil.make_archive(str(folder), "zip", folder)
    print(archive)
    return folder


def host_tests():
    run(["cmake", "-S", ROOT, "-B", ROOT / "build-host", "-DRC_HOST_BUILD=ON"])
    run(["cmake", "--build", ROOT / "build-host", "--config", "Debug"])
    run(["ctest", "--test-dir", ROOT / "build-host", "-C", "Debug", "--output-on-failure"])
    run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"])


def preview(source):
    source = Path(source or ROOT / ".tools/lvgl-preview").resolve()
    if not (source / "lvgl.h").is_file():
        raise RuntimeError("请先检出 LVGL v9.4.0 到 .tools/lvgl-preview，或用 --lvgl-source 指定；详见 docs/design/README.md。")
    version = (source / "lv_version.h").read_text(encoding="utf-8")
    if not all(f"#define LVGL_VERSION_{key} {value}" in version for key, value in (("MAJOR", 9), ("MINOR", 4), ("PATCH", 0))):
        raise RuntimeError("原生预览必须使用 LVGL 9.4.0。")
    build = ROOT / "build-preview"
    run(["cmake", "-S", ROOT, "-B", build, "-DRC_HOST_BUILD=ON", "-DRC_UI_PREVIEW=ON", f"-DLVGL_SOURCE_DIR={source}"])
    run(["cmake", "--build", build, "--config", "Debug", "--parallel", "8"])
    run(["ctest", "--test-dir", build, "-C", "Debug", "--output-on-failure"])
    binary = build / "tests/preview/ui_preview"
    if os.name == "nt":
        binary = build / "tests/preview/Debug/ui_preview.exe"
        if not binary.exists():
            binary = build / "tests/preview/ui_preview.exe"
    output = ROOT / "docs/design/rendered"
    output.mkdir(parents=True, exist_ok=True)
    for scenario in ("ready", "blocked", "status", "details", "pairing", "connecting", "confirm", "error"):
        raw = build / f"{scenario}.ppm"
        run([binary, raw, scenario])
        run([sys.executable, ROOT / "tools/ppm_to_png.py", raw, output / f"{scenario}.png"])


def doctor():
    print(f"Python: {sys.version.split()[0]}  系统: {sys.platform}")
    for program in ("git", "cmake", "clang-format"):
        print(f"{program}: {shutil.which(program) or '未安装'}")
    path, env = idf_environment()
    run([idf_python(env), path / "tools/idf.py", "--version"], env=env)
    run([idf_python(env), "-m", "serial.tools.list_ports"], env=env)
    print("SDK 与 Python 环境检查通过。")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["doctor", "setup", "build", "flash", "monitor", "erase", "test", "preview", "format-check", "package"])
    parser.add_argument("--port")
    parser.add_argument("--lvgl-source")
    parser.add_argument("--confirm-erase", action="store_true", help="确认清除全部 Flash，包括配对记录")
    options = parser.parse_args()
    if options.command == "setup":
        setup()
    elif options.command == "doctor":
        doctor()
    elif options.command == "test":
        host_tests()
    elif options.command == "preview":
        preview(options.lvgl_source)
    elif options.command == "package":
        package()
    elif options.command == "format-check":
        files = sorted(p for base in ("main", "components", "tests") for p in (ROOT / base).rglob("*") if p.suffix in (".cpp", ".hpp"))
        formatter = shutil.which("clang-format") or Path(sys.executable).parent / ("clang-format.exe" if os.name == "nt" else "clang-format")
        run([formatter, "--dry-run", "--Werror", *files])
    elif options.command == "build":
        idf(["build"])
    else:
        if options.command == "erase" and not options.confirm_erase:
            raise RuntimeError("全片擦除会删除配对；确实需要时加 --confirm-erase。")
        _, env = idf_environment()
        port = choose_port(options.port, env)
        idf(["-p", port, {"flash": "flash", "monitor": "monitor", "erase": "erase-flash"}[options.command]])


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError) as exc:
        print(f"错误：{exc}", file=sys.stderr)
        sys.exit(1)

