#!/usr/bin/env python3
"""Takes `amberedit --keytest` in AmberEdit's own Windows build.

There are no bytes to take here: PDCurses reads what the console reports for a
key and hands over a code of its own for it, `CTL_HOME` or `ALT_A`, so the
Windows build of --keytest writes those codes down instead, and the test played
over them asks what `poll()` makes of each. What the codes are is the console's
doing, and the two consoles a Windows user has are not the same one:

- **conhost** — the console window Windows has always had, started as
  `conhost.exe` so that a machine whose default terminal is Windows Terminal
  does not hand the program over to it.
- **terminal** — Windows Terminal, whose console is its own OpenConsole behind
  ConPTY: the key goes into the terminal, out as a sequence and back into a
  console record, and not every chord survives the trip.

The binary has to be built with -DAMBEREDIT_KEYTEST=ON.

    drive_windows_local.py --amberedit build\\bin\\amberedit.exe \\
                           --out testdata\\keys\\pdcurses [--terminal conhost ...]

A run is one file per console, `<terminal>.txt`: the protocols of
`modifier_key_reporting` are requests written to a terminal, and the Windows
build writes none. Keys are pressed through windows_input.py, under the same
rules as drive_windows_remote.py — this takes the desktop's keyboard and mouse
over, the window is checked to be in front before every key, Alt-F4 is never
pressed and the clipboard is emptied first. Started from an ssh login, it has
to go through a scheduled task that runs interactively (`schtasks /it`).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windows_input import (NEVER_PRESSED, NotInFront, empty_clipboard,  # noqa: E402
                           follow, in_front, layout_of, press, user32,
                           wait_for_window)


def powershell(command):
    return subprocess.run(["powershell", "-NoProfile", "-Command", command],
                          capture_output=True, text=True).stdout.strip()


def open_conhost(script, tag):
    proc = subprocess.Popen(["conhost.exe", "cmd.exe", "/c", script])
    # conhost puts "Administrator: " in front of the title of an elevated one.
    hwnd = wait_for_window(lambda pid, cls, title: title.endswith(tag)
                           and cls == "ConsoleWindowClass")
    return proc, hwnd


def open_windows_terminal(script, tag):
    proc = subprocess.Popen(["wt.exe", "-w", "new", "new-tab", "--title", tag,
                             "--suppressApplicationTitle", "cmd.exe", "/c", script])
    hwnd = wait_for_window(lambda pid, cls, title: title == tag
                           and cls == "CASCADIA_HOSTING_WINDOW_CLASS")
    return proc, hwnd


# name: (opener, the label the file is taken under).
TERMINALS = {
    "conhost": (open_conhost, lambda: "conhost-" + powershell(
        "(Get-Item $env:SystemRoot\\System32\\conhost.exe).VersionInfo.ProductVersion")),
    "terminal": (open_windows_terminal, lambda: "windows-terminal-" + powershell(
        "(Get-AppxPackage Microsoft.WindowsTerminal).Version")),
}


def take(terminal, amberedit, out_dir, wait_ms, only):
    opener, label_of = TERMINALS[terminal]
    out_path = os.path.abspath(os.path.join(out_dir, f"{terminal}.txt"))
    work = tempfile.mkdtemp(prefix="keytest-")
    sync_path = os.path.join(work, "sync")
    tag = f"amberedit-keytest-{uuid.uuid4().hex[:8]}"
    command = [os.path.abspath(amberedit), "--keytest", "--sync", sync_path,
               "--out", out_path, "--label", label_of(), "--wait", str(wait_ms)]
    if only:
        command += ["--only", only]
    # The title is how the window is found, and the line after the run is how
    # the driver learns it is over: neither console says when its program ends.
    script = os.path.join(work, "run.cmd")
    with open(script, "w") as f:
        f.write(f"@title {tag}\r\n")
        f.write(subprocess.list2cmdline(command) + "\r\n")
        f.write(f"@echo end>>\"{sync_path}\"\r\n")

    proc = hwnd = None
    try:
        proc, hwnd = opener(script, tag)
        if hwnd is None:
            return f"{terminal}: no window came up"
        next_line = follow(sync_path, lambda: not user32.IsWindow(hwnd))
        line = next_line(60)
        if line is None:
            return f"{terminal}: never started"
        layout = layout_of(hwnd)
        pressed = 0
        while line is not None:
            if line == "warmup":
                in_front(hwnd, "the warm-up")
                press("Space", layout)
            elif line == "flush":
                in_front(hwnd, "a flush")
                press("Esc", layout)
                press("Space", layout)
            elif line.startswith("ready "):
                name = line[len("ready "):]
                if name not in NEVER_PRESSED:
                    in_front(hwnd, name)
                    press(name, layout)
                    pressed += 1
            line = next_line(wait_ms / 1000 + 10)
        return f"{terminal}: {pressed} keys -> {out_path}"
    finally:
        if hwnd is not None and user32.IsWindow(hwnd):
            time.sleep(1)
            if user32.IsWindow(hwnd):
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        if proc is not None and proc.poll() is None:
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.terminate()
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amberedit", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--terminal", action="append", choices=sorted(TERMINALS))
    parser.add_argument("--wait", type=int, default=1500,
                        help="how long a key is waited for, in ms")
    parser.add_argument("--only", help="comma-separated keys, for a quick run")
    args = parser.parse_args()

    if not os.path.isfile(args.amberedit):
        print(f"no such binary: {args.amberedit}", file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    empty_clipboard()
    for terminal in args.terminal or list(TERMINALS):
        try:
            print(take(terminal, args.amberedit, args.out, args.wait, args.only),
                  flush=True)
        except NotInFront as stop:
            print(f"{terminal}: {stop}", flush=True)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
