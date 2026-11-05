#!/usr/bin/env python3
"""Takes `amberedit --keytest` through Windows terminals logged in to a Unix host.

AmberEdit is not run on Windows here: it runs on the host, over ssh, and the
Windows terminal in front of it is what turns a key into bytes — PuTTY's own
encoding, or Windows Terminal's through ConPTY and OpenSSH's ssh.exe. Those
bytes are what a sysop at a Windows desktop reading mail on a server sends, and
they land on the host's pty exactly as a Linux terminal's would, so the files
are the same files and are played back by the same test.

Keys are pressed through SendInput, by virtual key with the scan code beside it
and every modifier held by hand, so that the terminal gets what a keyboard
would have given it. That needs the desktop: this runs in the session of the
user logged on at the console, not in the one an ssh login gets — started from
ssh, through a scheduled task that runs interactively (`schtasks /it`).

    drive_windows_remote.py --host USER@HOST --amberedit build/bin/amberedit \\
                            --out keys-windows \\
                            [--terminal putty ...] [--protocol "xterm kitty" ...]

`--amberedit` and `--out` are paths on the host, relative ones read from the
home directory there, and ssh.exe has to log in there without asking anything.
PuTTY is looked for on PATH, where its installer puts it.

- **putty** — PuTTY as it ships, with the connection handed to ssh.exe as a
  local proxy command over the raw protocol, so that PuTTY needs no key of its
  own. Its terminal type is what PuTTY sends over ssh by default, `xterm`, and
  a saved session `amberedit-keytest` holds the three settings that differ:
  local echo and line editing off, which the raw protocol turns on, and the
  window closed when the connection ends.
- **putty-xterm216** — the same, with the keyboard set up as KEYS.md says:
  function keys "Xterm 216+", the arrows' modifiers "xterm-style bitmap" and
  application cursor keys disabled.
- **terminal** — Windows Terminal, a window of its own running `ssh -t`, under the title
  the driver gives it.

**This takes over the keyboard and the mouse of the desktop it runs on.**
Before every key the driver checks that the window it opened is the one in
front, clicks into it where it is not, and stops the whole run where that does
not bring it forward. Alt-F4 is never pressed: Windows closes the window with
it whatever the terminal would have sent. The clipboard is emptied first, so
that what Shift-Ins and Ctrl-V paste is nothing.

collapse.py then folds the four files a terminal gives into one per set of
bytes it sends, as for the other drivers.
"""

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import winreg

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windows_input import (NEVER_PRESSED, NotInFront, empty_clipboard,  # noqa: E402
                           follow, in_front, layout_of, press, user32,
                           wait_for_window)

PROTOCOLS = ["xterm kitty", "kitty xterm", "kitty", "xterm"]

def ssh(host, *words, input=None):
    """One command on the host. ssh.exe on Windows hangs before it has so much
    as greeted the server when any of its handles is a pipe or a file opened
    for deleting on close, so it is handed plain files: what it sends, what it
    says and what it complains of."""
    work = tempfile.mkdtemp(prefix="keytest-ssh-")
    try:
        paths = [os.path.join(work, n) for n in ("in", "out", "err")]
        with open(paths[0], "wb") as f:
            f.write((input or "").encode())
        with open(paths[0], "rb") as given, open(paths[1], "wb") as out, \
                open(paths[2], "wb") as err:
            done = subprocess.run(["ssh.exe", "-o", "BatchMode=yes", host, *words],
                                  stdin=given, stdout=out, stderr=err)
        said = [open(p, "rb").read().decode(errors="replace") for p in paths[1:]]
        return subprocess.CompletedProcess(done.args, done.returncode, *said)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def putty():
    found = shutil.which("putty.exe")
    if found is None:
        raise SystemExit("putty.exe is not on PATH")
    return found


PUTTY_SESSION = "amberedit-keytest"


# The keyboard as PuTTY ships it, and as KEYS.md tells a user to set it up:
# function keys and keypad "Xterm 216+", Shift/Ctrl/Alt with the arrows
# "xterm-style bitmap", and application cursor keys disabled — without the
# last, a terminal in application mode, which ncurses always asks for, is sent
# every arrow bare whatever is held with it.
KEYBOARD = {
    "putty": {"LinuxFunctionKeys": 0, "ShiftedArrowKeys": 0, "NoApplicationCursors": 0},
    "putty-xterm216": {"LinuxFunctionKeys": 6, "ShiftedArrowKeys": 1,
                       "NoApplicationCursors": 1},
}


def putty_session(keyboard):
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                           rf"Software\SimonTatham\PuTTY\Sessions\{PUTTY_SESSION}")
    with key:
        winreg.SetValueEx(key, "LocalEcho", 0, winreg.REG_DWORD, 1)  # FORCE_OFF
        winreg.SetValueEx(key, "LocalEdit", 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(key, "CloseOnExit", 0, winreg.REG_DWORD, 2)  # always
        for name, value in KEYBOARD[keyboard].items():
            winreg.SetValueEx(key, name, 0, winreg.REG_DWORD, value)


def file_version(path):
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"(Get-Item '{path}').VersionInfo.ProductVersion"],
                         capture_output=True, text=True).stdout.strip()
    return out.replace("Release ", "")


def open_putty(host, script, tag, keyboard="putty"):
    putty_session(keyboard)
    proxy = f"ssh.exe -tt -o BatchMode=yes {host} sh {script}"
    proc = subprocess.Popen([putty(), "-load", PUTTY_SESSION, "-raw",
                             "-proxycmd", proxy, host.split("@")[-1]])
    hwnd = wait_for_window(lambda pid, cls, title: pid == proc.pid and cls == "PuTTY")
    return proc, hwnd


def open_windows_terminal(host, script, tag):
    proc = subprocess.Popen(["wt.exe", "-w", "new", "new-tab", "--title", tag,
                             "--suppressApplicationTitle",
                             "ssh.exe", "-t", "-o", "BatchMode=yes", host, "sh", script])
    hwnd = wait_for_window(lambda pid, cls, title: title == tag
                           and cls == "CASCADIA_HOSTING_WINDOW_CLASS")
    return proc, hwnd


# name: (opener, TERM forced on the host or None, version). A file is called
# windows-<name>-<protocol>.txt.
TERMINALS = {
    "putty": (open_putty, "xterm", lambda: file_version(putty())),
    "putty-xterm216": (lambda host, script, tag: open_putty(host, script, tag,
                                                            "putty-xterm216"),
                       "xterm", lambda: file_version(putty())),
    "terminal": (open_windows_terminal, None, lambda: subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "(Get-AppxPackage Microsoft.WindowsTerminal).Version"],
        capture_output=True, text=True).stdout.strip()),
}


def take(terminal, protocol, host, amberedit, out_dir, wait_ms, only, version):
    opener, term, _ = TERMINALS[terminal]
    name = terminal if terminal.startswith("putty") else f"windows-{terminal}"
    label = f"{name}-{version}" if version else name
    stem = f"windows-{terminal}-{protocol.replace(' ', '-')}"
    out_path = f"{out_dir}/{stem}.txt"

    made = ssh(host, "d=$(mktemp -d) && mkfifo $d/sync && echo $d")
    work = made.stdout.strip()
    if made.returncode != 0 or not work:
        return f"{stem}: no working directory on {host}: {made.stderr.strip()}"
    command = [amberedit, "--keytest", protocol, "--sync", f"{work}/sync",
               "--out", out_path, "--label", label, "--wait", str(wait_ms)]
    if only:
        command += ["--only", only]
    lines = ["export LANG=C.UTF-8"]
    if term:
        lines.append(f"export TERM={term}")
    lines.append("exec " + " ".join(shlex.quote(c) for c in command))
    ssh(host, f"cat > {work}/run.sh", input="\n".join(lines) + "\n")

    # The FIFO is read on the host and its lines come over a second ssh, into
    # a file that is followed as it grows: ssh.exe will not write into a pipe.
    sync_dir = tempfile.mkdtemp(prefix="keytest-")
    sync_path = os.path.join(sync_dir, "sync")
    sync_out = open(sync_path, "wb")
    sync = subprocess.Popen(["ssh.exe", "-o", "BatchMode=yes", host,
                             f"cat {work}/sync"], stdin=subprocess.DEVNULL,
                            stdout=sync_out, stderr=subprocess.DEVNULL)
    next_line = follow(sync_path, lambda: sync.poll() is not None)

    tag = f"amberedit-keytest-{uuid.uuid4().hex[:8]}"
    proc = hwnd = None
    try:
        proc, hwnd = opener(host, f"{work}/run.sh", tag)
        if hwnd is None:
            return f"{stem}: no window came up"
        line = next_line(60)
        if line is None:
            return f"{stem}: never started"
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
        return f"{stem}: {pressed} keys -> {out_path}"
    finally:
        if hwnd is not None and user32.IsWindow(hwnd):
            time.sleep(1)
            if user32.IsWindow(hwnd):
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        if proc is not None and terminal.startswith("putty") and proc.poll() is None:
            proc.terminate()
        if sync.poll() is None:
            sync.terminate()
        sync.wait()
        sync_out.close()
        shutil.rmtree(sync_dir, ignore_errors=True)
        ssh(host, f"rm -rf {work}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--amberedit", required=True, help="the binary, on the host")
    parser.add_argument("--out", required=True, help="a directory on the host")
    parser.add_argument("--terminal", action="append", choices=sorted(TERMINALS))
    parser.add_argument("--protocol", action="append")
    parser.add_argument("--wait", type=int, default=1500,
                        help="how long a key is waited for, in ms")
    parser.add_argument("--only", help="comma-separated keys, for a quick run")
    args = parser.parse_args()

    made = ssh(args.host, "mkdir", "-p", args.out)
    if made.returncode != 0:
        print(f"cannot log in to {args.host}: {made.stderr.strip()}", file=sys.stderr)
        return 2
    empty_clipboard()
    for terminal in args.terminal or list(TERMINALS):
        version = TERMINALS[terminal][2]()
        for protocol in args.protocol or PROTOCOLS:
            try:
                print(take(terminal, protocol, args.host, args.amberedit, args.out,
                           args.wait, args.only, version), flush=True)
            except NotInFront as stop:
                print(f"{terminal}: {stop}", flush=True)
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
