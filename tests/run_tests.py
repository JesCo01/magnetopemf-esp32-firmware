"""Compila y ejecuta el parser/controlador reales con ArduinoJson y periféricos simulados.

Uso: python tests/run_tests.py --arduino-json RUTA/ArduinoJson/src
Requiere MSVC Build Tools en Windows o g++/clang++ en Linux.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-json", type=Path, required=True)
    args = parser.parse_args()
    if not (args.arduino_json / "ArduinoJson.h").is_file():
        parser.error("--arduino-json debe apuntar al directorio src de ArduinoJson 7.")
    tests = Path(__file__).resolve().parent
    firmware = tests.parent / "MagnetoPEMF"
    build = tests.parent / "build" / "firmware_command_tests"
    build.mkdir(parents=True, exist_ok=True)
    sources = [firmware / "CommandHandler.cpp", firmware / "MagnetoController.cpp",
               tests / "command_validation_test.cpp"]
    includes = [tests / "stubs", firmware, args.arduino_json.resolve()]
    services = tests / "stubs" / "TestServices.h"
    environment = os.environ.copy()
    if os.name == "nt":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
            "Microsoft Visual Studio/Installer/vswhere.exe")
        installation = subprocess.check_output(
            [str(vswhere), "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"], text=True).strip()
        setup = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
        comspec = os.environ.get("COMSPEC", "cmd.exe")
        result = subprocess.check_output(
            f'"{comspec}" /d /s /c ""{setup}" >nul && set"',
            text=True, errors="replace")
        for line in result.splitlines():
            if "=" in line and not line.startswith("="):
                name, value = line.split("=", 1)
                environment[name] = value
        compiler = shutil.which("cl.exe", path=environment.get("Path", environment.get("PATH")))
        if compiler is None:
            raise RuntimeError("No se encontró cl.exe tras cargar vcvars64.bat.")
        executable = build / "command_validation_test.exe"
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/W3", "/wd4996",
                   *[f"/I{directory}" for directory in includes], f"/FI{services}",
                   *map(str, sources), f"/Fe:{executable}", f"/Fo:{build}{os.sep}"]
    else:
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            raise RuntimeError("Se requiere g++ o clang++.")
        executable = build / "command_validation_test"
        command = [compiler, "-std=c++20", "-Wno-deprecated-declarations",
                   *[f"-I{directory}" for directory in includes], "-include", str(services),
                   *map(str, sources), "-o", str(executable)]
    subprocess.run(command, cwd=build, env=environment, check=True)
    subprocess.run([str(executable)], cwd=build, env=environment, check=True)


if __name__ == "__main__":
    main()
