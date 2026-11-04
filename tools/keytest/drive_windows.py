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

    drive_windows.py --host USER@HOST --amberedit build/bin/amberedit \\
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
import ctypes
import ctypes.wintypes as wt
import os
import queue
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import winreg

PROTOCOLS = ["xterm kitty", "kitty xterm", "kitty", "xterm"]

user32 = ctypes.WinDLL("user32", use_last_error=True)

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP = 0x1, 0x2
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x2, 0x4
ULONG_PTR = ctypes.c_size_t


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _INPUTUNION)]


user32.SendInput.argtypes = [wt.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.GetForegroundWindow.restype = wt.HWND
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetKeyboardLayout.restype = ctypes.c_void_p
user32.MapVirtualKeyExW.argtypes = [wt.UINT, wt.UINT, ctypes.c_void_p]
user32.VkKeyScanExW.argtypes = [wt.WCHAR, ctypes.c_void_p]
user32.VkKeyScanExW.restype = ctypes.c_short
user32.SetForegroundWindow.argtypes = [wt.HWND]
user32.IsWindow.argtypes = [wt.HWND]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
user32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
user32.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
user32.IsWindowVisible.argtypes = [wt.HWND]
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]

VK = {
    "Backspace": 0x08, "Tab": 0x09, "Enter": 0x0D, "Esc": 0x1B, "Space": 0x20,
    "PgUp": 0x21, "PgDn": 0x22, "End": 0x23, "Home": 0x24,
    "Left": 0x25, "Up": 0x26, "Right": 0x27, "Down": 0x28,
    "Ins": 0x2D, "Del": 0x2E,
    **{f"F{n}": 0x6F + n for n in range(1, 13)},
}
# The keys of the block between the letters and the keypad: the same scan codes
# as the keypad's, told apart by the extended flag.
EXTENDED = {"PgUp", "PgDn", "End", "Home", "Left", "Up", "Right", "Down", "Ins", "Del"}
MODIFIERS = {"ctrl": 0xA2, "alt": 0xA4, "shift": 0xA0}  # the left-hand ones
NEVER_PRESSED = {"Alt-F4"}


def chord(name, layout):
    """The modifiers and the virtual key of a key named the way --keytest names
    it, the punctuation looked up in the layout the terminal is typing in."""
    mods = []
    rest = name
    while True:
        for prefix, mod in (("Ctrl-", "ctrl"), ("Alt-", "alt"), ("Shift-", "shift")):
            if rest.startswith(prefix) and len(rest) > len(prefix):
                mods.append(mod)
                rest = rest[len(prefix):]
                break
        else:
            break
    if rest in VK:
        return mods, rest, VK[rest]
    if len(rest) == 1 and rest.isalpha():
        if rest.isupper() and not mods:
            mods.append("shift")
        return mods, rest, ord(rest.upper())
    if len(rest) == 1:
        found = user32.VkKeyScanExW(rest, layout)
        if found == -1:
            raise ValueError(f"no key in this layout types {name!r}")
        if found & 0x100:
            mods.append("shift")
        return mods, rest, found & 0xFF
    raise ValueError(f"no virtual key for {name!r}")


def key_input(vk, up, extended, layout):
    flags = (KEYEVENTF_KEYUP if up else 0) | (KEYEVENTF_EXTENDEDKEY if extended else 0)
    scan = user32.MapVirtualKeyExW(vk, 0, layout)  # MAPVK_VK_TO_VSC
    event = INPUT(type=INPUT_KEYBOARD)
    event.u.ki = KEYBDINPUT(vk, scan, flags, 0, 0)
    return event


def send(events):
    array = (INPUT * len(events))(*events)
    if user32.SendInput(len(events), array, ctypes.sizeof(INPUT)) != len(events):
        raise OSError(ctypes.get_last_error(), "SendInput refused the events")


def press(name, layout):
    mods, key, vk = chord(name, layout)
    events = [key_input(MODIFIERS[m], False, False, layout) for m in mods]
    events.append(key_input(vk, False, key in EXTENDED, layout))
    events.append(key_input(vk, True, key in EXTENDED, layout))
    events += [key_input(MODIFIERS[m], True, False, layout) for m in reversed(mods)]
    send(events)
    time.sleep(0.05)


def click(hwnd):
    """A click in the middle of the window, which is what gives a window the
    keyboard when SetForegroundWindow is refused it."""
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    point = wt.POINT(rect.right // 2, rect.bottom // 2)
    user32.ClientToScreen(hwnd, ctypes.byref(point))
    user32.SetCursorPos(point.x, point.y)
    events = []
    for flag in (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP):
        event = INPUT(type=INPUT_MOUSE)
        event.u.mi = MOUSEINPUT(0, 0, 0, flag, 0, 0)
        events.append(event)
    send(events)


def windows():
    """Every visible top-level window: its handle, process, class and title."""
    found = []

    def each(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            cls = ctypes.create_unicode_buffer(256)
            title = ctypes.create_unicode_buffer(512)
            user32.GetClassNameW(hwnd, cls, 256)
            user32.GetWindowTextW(hwnd, title, 512)
            found.append((hwnd, pid.value, cls.value, title.value))
        return True

    user32.EnumWindows(WNDENUMPROC(each), 0)
    return found


def wait_for_window(test, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for hwnd, pid, cls, title in windows():
            if test(pid, cls, title):
                return hwnd
        time.sleep(0.2)
    return None


def layout_of(hwnd):
    return user32.GetKeyboardLayout(user32.GetWindowThreadProcessId(hwnd, None))


def empty_clipboard():
    if user32.OpenClipboard(None):
        user32.EmptyClipboard()
        user32.CloseClipboard()


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


def putty_session():
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                           rf"Software\SimonTatham\PuTTY\Sessions\{PUTTY_SESSION}")
    with key:
        winreg.SetValueEx(key, "LocalEcho", 0, winreg.REG_DWORD, 1)  # FORCE_OFF
        winreg.SetValueEx(key, "LocalEdit", 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(key, "CloseOnExit", 0, winreg.REG_DWORD, 2)  # always


def file_version(path):
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"(Get-Item '{path}').VersionInfo.ProductVersion"],
                         capture_output=True, text=True).stdout.strip()
    return out.replace("Release ", "")


def open_putty(host, script, tag):
    putty_session()
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
    "terminal": (open_windows_terminal, None, lambda: subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "(Get-AppxPackage Microsoft.WindowsTerminal).Version"],
        capture_output=True, text=True).stdout.strip()),
}


class NotInFront(Exception):
    pass


def take(terminal, protocol, host, amberedit, out_dir, wait_ms, only, version):
    opener, term, _ = TERMINALS[terminal]
    name = "putty" if terminal == "putty" else f"windows-{terminal}"
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
    lines_in = queue.Queue()

    def reader():
        with open(sync_path, "rb") as f:
            pending = b""
            while True:
                chunk = f.read()
                if chunk:
                    pending += chunk
                    while b"\n" in pending:
                        line, _, pending = pending.partition(b"\n")
                        lines_in.put(line.decode("utf-8", "replace"))
                elif sync.poll() is not None:
                    break
                else:
                    time.sleep(0.02)
        lines_in.put(None)

    threading.Thread(target=reader, daemon=True).start()

    def next_line(timeout):
        try:
            return lines_in.get(timeout=timeout)
        except queue.Empty:
            return None

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

        def in_front(what):
            if user32.GetForegroundWindow() == hwnd:
                return
            user32.SetForegroundWindow(hwnd)
            time.sleep(0.3)
            if user32.GetForegroundWindow() != hwnd:
                click(hwnd)
                time.sleep(0.3)
            if user32.GetForegroundWindow() != hwnd:
                raise NotInFront(f"{stem}: the window is not in front at {what}; "
                                 "stopped before pressing anything else")

        while line is not None:
            if line == "warmup":
                in_front("the warm-up")
                press("Space", layout)
            elif line == "flush":
                in_front("a flush")
                press("Esc", layout)
                press("Space", layout)
            elif line.startswith("ready "):
                name = line[len("ready "):]
                if name not in NEVER_PRESSED:
                    in_front(name)
                    press(name, layout)
                    pressed += 1
            line = next_line(wait_ms / 1000 + 10)
        return f"{stem}: {pressed} keys -> {out_path}"
    finally:
        if hwnd is not None and user32.IsWindow(hwnd):
            time.sleep(1)
            if user32.IsWindow(hwnd):
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        if proc is not None and terminal == "putty" and proc.poll() is None:
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
                print(stop, flush=True)
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
