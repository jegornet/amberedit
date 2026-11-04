#!/usr/bin/env python3
"""Takes `amberedit --keytest` in macOS terminals without anybody at the keyboard.

The macOS counterpart of drive_x11.py. Each terminal is opened on a window of
its own running `amberedit --keytest`, and the keys it asks for are posted
through Quartz — CGEventPost at the HID level, by virtual key code with the
modifier flags written out — so that the terminal gets what a keyboard would
have given it and encodes the key itself.

The binary has to be built with -DAMBEREDIT_KEYTEST=ON, and the process this
runs under needs the right to post events: System Settings, Privacy & Security,
Accessibility, for the terminal application it is started from. --request asks
for it and exits.

**This takes over the keyboard of the machine it runs on.** The events go to
whatever window is in front, so before every key the driver checks that the
terminal it opened is the frontmost process — and for iTerm2 and Terminal.app,
whose windows share one process, that its window is the current one — and
stops the whole run at once where it is not. Leave the machine alone while it
runs.

    drive_macos.py --amberedit build/bin/amberedit --out testdata/keys \\
                   [--terminal iterm2 ...] [--protocol "xterm kitty" ...]

collapse.py then folds the four files a terminal gives into one per set of
bytes it sends, as for drive_x11.py.
"""

import argparse
import ctypes
import ctypes.util
import os
import plistlib
import select
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

PROTOCOLS = ["xterm kitty", "kitty xterm", "kitty", "xterm"]

# kVK_* from HIToolbox/Events.h: positions on an ANSI keyboard, not characters.
KEYCODES = {
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8,
    "v": 9, "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
    "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "=": 24, "9": 25,
    "7": 26, "-": 27, "8": 28, "0": 29, "]": 30, "o": 31, "u": 32, "[": 33,
    "i": 34, "p": 35, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41, "\\": 42,
    ",": 43, "/": 44, "n": 45, "m": 46, ".": 47, "`": 50,
    "Enter": 36, "Tab": 48, "Space": 49, "Backspace": 51, "Esc": 53,
    "F1": 122, "F2": 120, "F3": 99, "F4": 118, "F5": 96, "F6": 97, "F7": 98,
    "F8": 100, "F9": 101, "F10": 109, "F11": 103, "F12": 111,
    # A Mac keyboard has no Insert; Help stands where it stood, and is what
    # terminals that offer Insert at all read as Insert.
    "Ins": 114, "Home": 115, "PgUp": 116, "Del": 117, "End": 119, "PgDn": 121,
    "Left": 123, "Right": 124, "Down": 125, "Up": 126,
}
# The characters a key with Shift held types.
SHIFTED = {"+": "=", "_": "-", "?": "/", ":": ";", '"': "'", "<": ",", ">": ".",
           "{": "[", "}": "]", "|": "\\", "~": "`", "!": "1", "@": "2", "#": "3",
           "$": "4", "%": "5", "^": "6", "&": "7", "*": "8", "(": "9", ")": "0"}

SHIFT, CONTROL, OPTION = 0x20000, 0x40000, 0x80000
NUMERIC_PAD, FUNCTION = 0x200000, 0x800000
MODIFIER_KEYS = {SHIFT: 56, CONTROL: 59, OPTION: 58}
ARROWS = {"Left", "Right", "Up", "Down"}
# The keys a Mac keyboard reaches with fn, which it reports with the flag on.
FN_KEYS = ARROWS | {"Ins", "Home", "End", "PgUp", "PgDn", "Del"} | \
    {f"F{n}" for n in range(1, 13)}


def chord(name):
    """The flags and the key code of a key named the way --keytest names it."""
    flags = 0
    rest = name
    while True:
        for prefix, flag in (("Ctrl-", CONTROL), ("Alt-", OPTION), ("Shift-", SHIFT)):
            if rest.startswith(prefix) and len(rest) > len(prefix):
                flags |= flag
                rest = rest[len(prefix):]
                break
        else:
            break
    if rest in KEYCODES:
        key = rest
    elif len(rest) == 1 and rest.isalpha():
        key = rest.lower()
        if rest.isupper() and not flags:
            flags |= SHIFT
    elif rest in SHIFTED:
        key = SHIFTED[rest]
        flags |= SHIFT
    else:
        raise ValueError(f"no key code for {name!r}")
    if key in FN_KEYS:
        flags |= FUNCTION
    if key in ARROWS:
        flags |= NUMERIC_PAD
    return flags, KEYCODES[key]


class Quartz:
    """CGEventPost over ctypes, the system Python having no PyObjC."""

    def __init__(self):
        path = ctypes.util.find_library("ApplicationServices")
        self.cg = ctypes.CDLL(path)
        self.cg.CGEventCreateKeyboardEvent.restype = ctypes.c_void_p
        self.cg.CGEventCreateKeyboardEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint16,
                                                       ctypes.c_bool]
        self.cg.CGEventSetFlags.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        self.cg.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
        self.cg.CFRelease.argtypes = [ctypes.c_void_p]
        self.cg.CGPreflightPostEventAccess.restype = ctypes.c_bool
        self.cg.CGRequestPostEventAccess.restype = ctypes.c_bool

    def allowed(self):
        return self.cg.CGPreflightPostEventAccess()

    def request(self):
        return self.cg.CGRequestPostEventAccess()

    def post(self, code, down, flags):
        event = self.cg.CGEventCreateKeyboardEvent(None, code, down)
        self.cg.CGEventSetFlags(event, flags)
        self.cg.CGEventPost(0, event)  # kCGHIDEventTap
        self.cg.CFRelease(event)

    def press(self, name):
        flags, code = chord(name)
        held = 0
        for flag, modifier in MODIFIER_KEYS.items():
            if flags & flag:
                held |= flag
                self.post(modifier, True, held)
        self.post(code, True, flags)
        self.post(code, False, flags)
        for flag, modifier in reversed(list(MODIFIER_KEYS.items())):
            if flags & flag:
                held &= ~flag
                self.post(modifier, False, held)


def osascript(script):
    done = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    return done.stdout.strip()


def front_pid():
    """The process in front, as System Events sees it: lsappinfo answers NULL
    for most of what it is asked on a current macOS."""
    pid = osascript('tell application "System Events" to get unix id of '
                    "first process whose frontmost is true")
    return int(pid) if pid.isdigit() else None


def bring_forward(pid):
    osascript('tell application "System Events" to set frontmost of '
              f"(first process whose unix id is {pid}) to true")


def app_version(app):
    try:
        with open(os.path.join(app, "Contents", "Info.plist"), "rb") as f:
            return plistlib.load(f).get("CFBundleShortVersionString", "")
    except OSError:
        return ""


class Opened:
    """A terminal window running the script: its process, and how to tell
    whether that window is still the one in front."""

    def __init__(self, pid, window_check=None, close=None, proc=None):
        self.pid = pid
        self.window_check = window_check
        self.close = close
        self.proc = proc

    def in_front(self):
        if front_pid() != self.pid:
            return False
        return self.window_check is None or self.window_check()


def wait_for_pid(name, timeout=20):
    """The pid of the application process System Events calls `name` — pgrep
    does not find iTerm2 by its name."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pid = osascript(f'tell application "System Events" to get unix id of '
                        f'first process whose name is "{name}"')
        if pid.isdigit():
            return int(pid)
        time.sleep(0.2)
    return None


def open_iterm2(script):
    window = osascript(f'''tell application "iTerm2"
        set w to (create window with default profile command "{script}")
        activate
        return id of w
    end tell''')
    pid = wait_for_pid("iTerm2")

    def check():
        return osascript('tell application "iTerm2" to return id of current window') \
            == window

    def close():
        osascript(f'tell application "iTerm2" to close (windows whose id is {window})')

    return Opened(pid, check, close)


def open_terminal_app(script):
    window = osascript(f'''tell application "Terminal"
        do script "exec {script}"
        activate
        return id of front window
    end tell''')
    pid = wait_for_pid("Terminal")

    def check():
        return osascript('tell application "Terminal" to return id of front window') \
            == window

    def close():
        osascript(f'tell application "Terminal" to close (windows whose id is {window})')

    return Opened(pid, check, close)


def pids_of(name):
    found = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
    return {int(p) for p in found.stdout.split()}


def open_binary(argv, gui):
    """A terminal started as a process of its own. The window belongs to the
    process named `gui`, which need not be the one started — `wezterm start`
    hands over to a wezterm-gui of its own — so it is the one of that name that
    was not there before."""
    def opener(script):
        before = pids_of(gui)
        proc = subprocess.Popen(argv + [script], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 20
        pid = None
        while pid is None and time.monotonic() < deadline:
            new = pids_of(gui) - before
            pid = max(new) if new else None
            time.sleep(0.2)
        return Opened(pid if pid is not None else proc.pid, None, None, proc)
    return opener


WEZTERM = "/Applications/WezTerm.app/Contents/MacOS/wezterm"
KITTY = "/Applications/kitty.app/Contents/MacOS/kitty"

TERMINALS = {
    "iterm2": ("/Applications/iTerm.app", open_iterm2),
    "terminal-app": ("/System/Applications/Utilities/Terminal.app", open_terminal_app),
    "wezterm": ("/Applications/WezTerm.app", open_binary(
        [WEZTERM, "--skip-config", "start", "--always-new-process", "--"],
        "wezterm-gui")),
    "wezterm-kittykbd": ("/Applications/WezTerm.app", open_binary(
        [WEZTERM, "--skip-config", "--config", "enable_kitty_keyboard=true", "start",
         "--always-new-process", "--"], "wezterm-gui")),
    "kitty": ("/Applications/kitty.app", open_binary(
        [KITTY, "--config", "NONE", "--single-instance=no"], "kitty")),
}


def read_line(fd, buffer, timeout):
    deadline = time.monotonic() + timeout
    while b"\n" not in buffer[0]:
        left = deadline - time.monotonic()
        if left <= 0:
            return None
        ready, _, _ = select.select([fd], [], [], left)
        if not ready:
            return None
        chunk = os.read(fd, 4096)
        if not chunk:
            return None
        buffer[0] += chunk
    line, _, buffer[0] = buffer[0].partition(b"\n")
    return line.decode("utf-8", "replace")


class NotInFront(Exception):
    pass


def take(terminal, protocol, amberedit, out_dir, wait_ms, only, quartz):
    app, opener = TERMINALS[terminal]
    version = app_version(app)
    label = f"{terminal}-{version}" if version else terminal
    stem = f"{terminal}-{protocol.replace(' ', '-')}"
    out_path = os.path.abspath(os.path.join(out_dir, f"{stem}.txt"))

    work = tempfile.mkdtemp(prefix="keytest-")
    fifo = os.path.join(work, "sync")
    os.mkfifo(fifo)
    script = os.path.join(work, "run.sh")
    command = [os.path.abspath(amberedit), "--keytest", protocol,
               "--sync", fifo, "--out", out_path, "--label", label,
               "--wait", str(wait_ms)]
    if only:
        command += ["--only", only]
    with open(script, "w") as f:
        f.write("#!/bin/sh\nexec " + " ".join(shlex.quote(c) for c in command) + "\n")
    os.chmod(script, 0o755)

    opened = None
    fd = None
    try:
        fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
        opened = opener(script)
        buffer = [b""]
        line = read_line(fd, buffer, 60)
        if line is None:
            return f"{stem}: never started"
        bring_forward(opened.pid)
        time.sleep(0.5)
        pressed = 0

        def press_in_front(key, what):
            if not opened.in_front():
                bring_forward(opened.pid)
                time.sleep(0.5)
                if not opened.in_front():
                    raise NotInFront(f"{stem}: the window is not in front at {what}; "
                                     "stopped before pressing anything else")
            quartz.press(key)

        # What --keytest asks for, a line at a time, until it closes the FIFO.
        while line is not None:
            if line == "warmup":
                press_in_front("Space", "the warm-up")
            elif line == "flush":
                # Whatever the key opened, or the dead key it was waiting on a
                # letter for: Escape puts the first away and Space finishes
                # the second. --keytest throws out what they send.
                press_in_front("Esc", "a flush")
                quartz.press("Space")
            elif line.startswith("ready "):
                name = line[len("ready "):]
                press_in_front(name, name)
                pressed += 1
            line = read_line(fd, buffer, wait_ms / 1000 + 10)
        return f"{stem}: {pressed} keys -> {out_path}"
    finally:
        if fd is not None:
            os.close(fd)
        if opened is not None:
            if opened.close is not None:
                opened.close()
            if opened.proc is not None:
                if opened.pid != opened.proc.pid:
                    try:
                        os.kill(opened.pid, 15)
                    except ProcessLookupError:
                        pass
                if opened.proc.poll() is None:
                    opened.proc.terminate()
                    opened.proc.wait()
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amberedit")
    parser.add_argument("--out")
    parser.add_argument("--terminal", action="append", choices=sorted(TERMINALS))
    parser.add_argument("--protocol", action="append")
    parser.add_argument("--wait", type=int, default=2000,
                        help="how long a key is waited for, in ms")
    parser.add_argument("--only", help="comma-separated keys, for a quick run")
    parser.add_argument("--request", action="store_true",
                        help="ask for the right to post events, and exit")
    args = parser.parse_args()

    quartz = Quartz()
    if args.request:
        quartz.request()
        print("allowed" if quartz.allowed() else
              "not allowed yet: System Settings > Privacy & Security > Accessibility")
        return 0
    if not quartz.allowed():
        print("no right to post events; run with --request first", file=sys.stderr)
        return 2
    if not args.amberedit or not args.out:
        parser.error("--amberedit and --out are required")

    # iTerm2 last: a session driving this may well be running inside it.
    terminals = args.terminal or sorted(
        (t for t, (app, _) in TERMINALS.items() if os.path.isdir(app)),
        key=lambda t: t == "iterm2")
    os.makedirs(args.out, exist_ok=True)
    for terminal in terminals:
        for protocol in args.protocol or PROTOCOLS:
            try:
                print(take(terminal, protocol, args.amberedit, args.out, args.wait,
                           args.only, quartz), flush=True)
            except NotInFront as stop:
                print(stop, flush=True)
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
