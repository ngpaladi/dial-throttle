#!/usr/bin/env python3
"""Cross-platform installer for Dial Throttle firmware."""

from __future__ import annotations

import argparse
import glob
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

PROJECT_DIR = Path(__file__).resolve().parent
CONFIG_FILE = PROJECT_DIR / "include" / "config.h"


def say(message: str) -> None:
    print(f"==> {message}")


def warn(message: str) -> None:
    print(f"Warning: {message}")


def fail(message: str) -> None:
    print(f"Error: {message}")
    raise SystemExit(1)


def ask_yes_no(prompt: str, default_yes: bool, auto_yes: bool, auto_result: bool | None = None) -> bool:
    if auto_yes:
        return default_yes if auto_result is None else auto_result

    suffix = "[Y/n]" if default_yes else "[y/N]"
    while True:
        raw = input(f"{prompt} {suffix}: ").strip().lower()
        if not raw:
            return default_yes
        if raw in {"y", "yes"}:
            return True
        if raw in {"n", "no"}:
            return False
        print("Please enter y or n.")


def run_cmd(cmd: list[str], check: bool = True, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=PROJECT_DIR,
        check=check,
        text=True,
        capture_output=capture,
    )


def command_works(cmd: list[str]) -> bool:
    try:
        run_cmd(cmd + ["--version"], check=True, capture=True)
        return True
    except Exception:
        return False


def resolve_pio_command() -> list[str] | None:
    module_cmd = [sys.executable, "-m", "platformio"]
    if command_works(module_cmd):
        return module_cmd

    if shutil.which("pio"):
        return ["pio"]

    if shutil.which("platformio"):
        return ["platformio"]

    return None


def ensure_platformio(auto_yes: bool) -> list[str]:
    cmd = resolve_pio_command()
    if cmd is not None:
        return cmd

    warn("PlatformIO is not installed.")
    if not ask_yes_no("Install PlatformIO now with pip?", default_yes=True, auto_yes=auto_yes):
        fail("PlatformIO is required. Install it and run the installer again.")

    pip_cmd = [sys.executable, "-m", "pip", "install", "--user", "-U", "platformio"]
    say("Installing PlatformIO...")
    try:
        run_cmd(pip_cmd, check=True)
    except subprocess.CalledProcessError:
        fail("PlatformIO installation failed. Install manually and retry.")

    cmd = resolve_pio_command()
    if cmd is None:
        fail("PlatformIO installed but could not be invoked.")
    return cmd


def parse_device_list_json(raw_json: str) -> list[dict[str, str]]:
    try:
        items: Any = json.loads(raw_json)
    except json.JSONDecodeError:
        return []

    ports: list[dict[str, str]] = []
    if not isinstance(items, list):
        return ports

    for item in items:
        if not isinstance(item, dict):
            continue
        port = str(item.get("port") or "").strip()
        if not port:
            continue
        description = str(item.get("description") or item.get("hwid") or "").strip()
        ports.append({"port": port, "description": description})
    return ports


def detect_ports(pio_cmd: list[str]) -> list[dict[str, str]]:
    try:
        result = run_cmd(pio_cmd + ["device", "list", "--json-output"], check=True, capture=True)
        ports = parse_device_list_json(result.stdout)
        if ports:
            return ports
    except Exception:
        pass

    # Fallback path-based probe for Unix-like systems.
    ports: list[dict[str, str]] = []
    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/cu.usb*", "/dev/cu.SLAB*", "/dev/tty.usb*"):
        for path in sorted(glob.glob(pattern)):
            ports.append({"port": str(path), "description": ""})
    return ports


def choose_port_interactive(ports: list[dict[str, str]]) -> str:
    while True:
        print("\nDetected serial ports:")
        if not ports:
            print("  (none found)")
        else:
            for idx, item in enumerate(ports, start=1):
                desc = f" - {item['description']}" if item["description"] else ""
                print(f"  {idx}) {item['port']}{desc}")
        print("  m) Enter port manually")
        print("  r) Re-scan ports")

        choice = input("Select port: ").strip()
        if choice.lower() == "m":
            manual = input("Enter port (example: /dev/ttyACM0 or COM3): ").strip()
            if manual:
                return manual
            continue
        if choice.lower() == "r":
            return "__rescan__"
        if choice.isdigit():
            idx = int(choice)
            if 1 <= idx <= len(ports):
                return ports[idx - 1]["port"]
        print("Invalid selection.")


def select_port(pio_cmd: list[str], requested_port: str | None, auto_yes: bool) -> str:
    if requested_port:
        return requested_port

    ports = detect_ports(pio_cmd)

    if auto_yes:
        if not ports:
            fail("No serial port detected. Connect board or pass --port COM3 (Windows) or /dev/ttyACM0.")
        return ports[0]["port"]

    while True:
        selected = choose_port_interactive(ports)
        if selected == "__rescan__":
            ports = detect_ports(pio_cmd)
            continue
        return selected


def open_config_editor() -> None:
    editor = os.environ.get("EDITOR")
    system = platform.system()

    try:
        if editor:
            run_cmd(editor.split() + [str(CONFIG_FILE)], check=True)
            return

        if system == "Windows":
            run_cmd(["notepad", str(CONFIG_FILE)], check=True)
            return

        if system == "Darwin":
            run_cmd(["open", "-e", str(CONFIG_FILE)], check=True)
            return

        if shutil.which("nano"):
            run_cmd(["nano", str(CONFIG_FILE)], check=True)
            return
        if shutil.which("vi"):
            run_cmd(["vi", str(CONFIG_FILE)], check=True)
            return

        warn(f"No supported editor found. Edit this file manually: {CONFIG_FILE}")
    except subprocess.CalledProcessError:
        warn(f"Could not open editor. Edit this file manually: {CONFIG_FILE}")


def show_permissions_hint(port: str) -> None:
    if platform.system() != "Linux":
        return

    if not port.startswith("/dev/"):
        return

    if os.path.exists(port) and not os.access(port, os.R_OK | os.W_OK):
        warn(f"You may not have serial permission for {port}")
        print("Try:")
        print('  sudo usermod -aG dialout "$USER"')
        print("Then log out and back in.")


def determine_mode(flag_yes: bool, flag_no: bool, default_for_auto_yes: bool) -> str:
    if flag_yes:
        return "yes"
    if flag_no:
        return "no"
    return "auto_yes_default_true" if default_for_auto_yes else "auto_yes_default_false"


def should_enable(mode: str, auto_yes: bool, prompt: str, default_yes: bool) -> bool:
    if mode == "yes":
        return True
    if mode == "no":
        return False
    if auto_yes:
        return mode == "auto_yes_default_true"
    return ask_yes_no(prompt, default_yes=default_yes, auto_yes=False)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Dial Throttle cross-platform installer")
    parser.add_argument("-y", "--yes", action="store_true", help="Non-interactive mode")
    parser.add_argument("-p", "--port", help="Serial port (example: COM3 or /dev/ttyACM0)")

    monitor_group = parser.add_mutually_exclusive_group()
    monitor_group.add_argument("--monitor", action="store_true", help="Open serial monitor after upload")
    monitor_group.add_argument("--no-monitor", action="store_true", help="Do not open monitor after upload")

    edit_group = parser.add_mutually_exclusive_group()
    edit_group.add_argument("--edit-config", action="store_true", help="Open include/config.h before flashing")
    edit_group.add_argument("--no-edit-config", action="store_true", help="Skip config editor")

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    print("\nDial Throttle Installer (Linux/macOS/Windows)")
    print("This wizard builds and flashes firmware to a connected M5Stack Dial.")

    pio_cmd = ensure_platformio(auto_yes=args.yes)

    edit_mode = determine_mode(args.edit_config, args.no_edit_config, default_for_auto_yes=False)
    if should_enable(edit_mode, args.yes, prompt=f"Open {CONFIG_FILE} now?", default_yes=False):
        open_config_editor()

    port = select_port(pio_cmd, requested_port=args.port, auto_yes=args.yes)
    show_permissions_hint(port)

    print(f"\nReady to flash using: {port}")
    if not ask_yes_no("Continue?", default_yes=True, auto_yes=args.yes):
        fail("Installer canceled by user.")

    say("Building firmware...")
    run_cmd(pio_cmd + ["run"], check=True)

    say(f"Uploading firmware to {port}...")
    run_cmd(pio_cmd + ["run", "-t", "upload", "--upload-port", port], check=True)

    monitor_mode = determine_mode(args.monitor, args.no_monitor, default_for_auto_yes=False)
    if should_enable(monitor_mode, args.yes, prompt="Open serial monitor now?", default_yes=True):
        say("Opening serial monitor at 115200 baud (Ctrl+C to exit)...")
        run_cmd(pio_cmd + ["device", "monitor", "-b", "115200", "--port", port], check=True)
    else:
        print("Install complete.")
        print(f"Open monitor later with: {' '.join(pio_cmd)} device monitor -b 115200 --port {port}")


if __name__ == "__main__":
    main()
