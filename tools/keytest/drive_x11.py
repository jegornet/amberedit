#!/usr/bin/env python3
"""Takes `amberedit --keytest` in X11 terminals without anybody at the keyboard.

Each terminal is started on a display of its own under Xvfb, running
`amberedit --keytest`, and xdotool presses the keys it asks for. xdotool goes
through XTEST, so the terminal gets the same event a keyboard would have given
it and encodes the key itself — which is the whole point: anything that typed
the bytes into the pty directly would be taking down our own guess.

The binary has to be built with -DAMBEREDIT_KEYTEST=ON.

    drive_x11.py --amberedit build/bin/amberedit --out testdata/keys \\
                 [--terminal xterm ...] [--protocol "xterm kitty" ...]

With no --terminal, every terminal below that is installed is taken; with no
--protocol, all four values of modifier_key_reporting are. collapse.py then
folds the four files a terminal gives into one per set of bytes it sends, which
is the shape testdata/keys/ is kept in.
"""

import argparse
import ctypes
import ctypes.util
import os
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

# How each terminal is told to run one program. The program is always a single
# script with no spaces in its path, so the terminals that take a command as a
# string and those that take it as words are given the same thing.
TERMINALS = {
    "xterm": ["xterm", "-e"],
    "urxvt": ["urxvt", "-e"],
    "st": ["st", "-e"],
    "alacritty": ["alacritty", "-e"],
    "kitty": ["kitty", "--"],
    "wezterm": ["wezterm", "--skip-config", "start", "--always-new-process", "--"],
    # The same WezTerm with the kitty keyboard protocol it ships turned off.
    "wezterm-kittykbd": ["wezterm", "--skip-config", "--config",
                         "enable_kitty_keyboard=true", "start",
                         "--always-new-process", "--"],
    "konsole": ["konsole", "--separate", "-e"],
    "gnome-terminal": ["gnome-terminal", "--wait", "--"],
    "ptyxis": ["ptyxis", "--standalone", "--new-window", "-x"],
    "xfce4-terminal": ["xfce4-terminal", "--disable-server", "-x"],
    "mate-terminal": ["mate-terminal", "-x"],
    "terminator": ["terminator", "-u", "-x"],
    "tilix": ["tilix", "-e"],
    "lxterminal": ["lxterminal", "-e"],
    "qterminal": ["qterminal", "-e"],
    "sakura": ["sakura", "-x"],
    "contour": ["contour", "--"],
    "pterm": ["pterm", "-e"],
    # The multiplexers, each in an xterm: a layer of their own between the
    # terminal and AmberEdit, re-encoding what the terminal sent. {conf} and
    # {label} are filled in per run.
    "tmux": ["xterm", "-e", "tmux", "-L", "{label}", "-f", "{conf}", "new-session"],
    "tmux-extkeys": ["xterm", "-e", "tmux", "-L", "{label}", "-f", "{conf}",
                     "new-session"],
    "screen": ["xterm", "-e", "screen", "-c", "{conf}", "-S", "{label}"],
    # And tmux passing modified keys on from a terminal that reports them.
    "kitty-tmux": ["kitty", "--", "tmux", "-L", "{label}", "-f", "{conf}",
                           "new-session"],
}

# What a multiplexer is started with. Each has its prefix moved off the keys a
# run asks for — Ctrl-B and Ctrl-A are in the list, and the key after a prefix
# is a command to the multiplexer rather than a key for the program, which would
# put every answer after it out of step. That a multiplexer keeps its prefix for
# itself is known without taking it.
LAYERS = {
    "tmux": ("tmux", "set -g prefix None\nunbind C-b\n"),
    "tmux-extkeys": ("tmux", "set -g prefix None\nunbind C-b\n"
                             "set -s extended-keys on\n"
                             "set -as terminal-features 'xterm*:extkeys'\n"),
    "screen": ("screen", "escape ^]]\n"),
    "kitty-tmux": ("tmux", "set -g prefix None\nunbind C-b\n"
                                   "set -s extended-keys on\n"
                                   "set -as terminal-features '*:extkeys'\n"),
}

# The terminals that are a server behind a client: started under a D-Bus
# session of their own, or they look for one that is not there.
NEEDS_DBUS = {"gnome-terminal", "mate-terminal", "xfce4-terminal",
              "terminator", "tilix"}
# ptyxis is the exception: it starts its shell in a scope of the user's systemd
# instance, so it wants the session bus that instance answers on, not one of its
# own.

PROTOCOLS = ["xterm kitty", "kitty xterm", "kitty", "xterm"]

NAMED = {
    "Up": "Up", "Down": "Down", "Left": "Left", "Right": "Right",
    "Home": "Home", "End": "End", "PgUp": "Prior", "PgDn": "Next",
    "Ins": "Insert", "Del": "Delete", "Backspace": "BackSpace", "Tab": "Tab",
    "Enter": "Return", "Esc": "Escape", "Space": "space",
}
for n in range(1, 13):
    NAMED[f"F{n}"] = f"F{n}"

PUNCTUATION = {
    "-": "minus", "+": "plus", "=": "equal", "/": "slash", "\\": "backslash",
    ".": "period", ",": "comma", ";": "semicolon", "'": "apostrophe",
    "[": "bracketleft", "]": "bracketright", "`": "grave", "*": "asterisk",
    "?": "question", "!": "exclam", "<": "less", ">": "greater", ":": "colon",
    "#": "numbersign", "$": "dollar", "%": "percent", "&": "ampersand",
    "(": "parenleft", ")": "parenright", "_": "underscore", "|": "bar",
    "~": "asciitilde", "^": "asciicircum", "@": "at", '"': "quotedbl",
}


def keysym(name):
    """The modifiers and the keysym of a key named the way --keytest names it."""
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
    if rest in NAMED:
        key = NAMED[rest]
    elif len(rest) == 1 and rest.isalnum():
        # A chord is the letter as the terminal reports it, lower case; a bare
        # capital is Shift held, which xdotool works out from the keysym.
        key = rest.lower() if mods else rest
    elif rest in PUNCTUATION:
        key = PUNCTUATION[rest]
    else:
        raise ValueError(f"no keysym for {name!r}")
    return mods, key


MODIFIER_KEYSYMS = {"ctrl": "Control_L", "alt": "Alt_L", "shift": "Shift_L"}


def keycodes(env):
    """Keysym -> (keycode, needs Shift), off the display's own map.

    xdotool asked for a keysym by name picks the level it is typed at itself,
    and under Xvfb's map it picks one with Mod1 in it for the function keys:
    xterm then sends Alt-F1 for F1. So the key is pressed by its code, and
    every modifier is held by hand.
    """
    table = subprocess.run(["xmodmap", "-pke"], env=env, capture_output=True,
                           text=True).stdout
    codes = {}
    for row in table.splitlines():
        head, _, syms = row.partition("=")
        words = head.split()
        if len(words) != 2 or not words[1].isdigit():
            continue
        code = int(words[1])
        for level, sym in enumerate(syms.split()[:2]):
            codes.setdefault(sym, (code, level == 1))
    return codes


class Xtest:
    """XTEST over ctypes: libX11 and libXtst are on any machine with Xvfb on it,
    and a Python binding for them is one more package that has to match the
    interpreter."""

    def __init__(self, display):
        self.x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
        self.xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                                ctypes.c_int, ctypes.c_ulong]
        self.dpy = self.x11.XOpenDisplay(display.encode())
        if not self.dpy:
            raise RuntimeError(f"cannot open display {display}")

    def key(self, code, down):
        self.xtst.XTestFakeKeyEvent(self.dpy, code, 1 if down else 0, 0)

    def sync(self):
        self.x11.XSync(self.dpy, 0)

    def close(self):
        self.x11.XCloseDisplay(self.dpy)


def press(name, codes, display):
    """Presses the key through XTEST, by its code, with every modifier held by
    hand. xdotool cannot be asked for a code: it reads 0 to 9 as keysyms."""
    mods, key = keysym(name)
    code, shifted = codes[key]
    held = [MODIFIER_KEYSYMS[m] for m in mods]
    if shifted and "Shift_L" not in held:
        held.append("Shift_L")
    held_codes = [codes[m][0] for m in held]
    for c in held_codes:
        display.key(c, True)
    display.key(code, True)
    display.key(code, False)
    for c in reversed(held_codes):
        display.key(c, False)
    display.sync()


def free_display():
    """A display number no live X server holds. A lock left by a server that
    died without cleaning up names a pid that is gone, and the number is free."""
    for n in range(50, 200):
        lock = f"/tmp/.X{n}-lock"
        try:
            with open(lock) as f:
                pid = int(f.read().strip() or 0)
            os.kill(pid, 0)
            continue
        except FileNotFoundError:
            if not os.path.exists(f"/tmp/.X11-unix/X{n}"):
                return n
            continue
        except (ProcessLookupError, ValueError):
            for stale in (lock, f"/tmp/.X11-unix/X{n}"):
                try:
                    os.remove(stale)
                except OSError:
                    pass
            return n
        except PermissionError:
            continue
    raise RuntimeError("no free display")


def kill_group(proc):
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    proc.wait()


def focus_terminal(env):
    """Gives the keyboard to the terminal: the newest visible window there is,
    there being no window manager to have done it."""
    found = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "."],
                           env=env, capture_output=True, text=True)
    windows = found.stdout.split()
    if not windows:
        return None
    subprocess.run(["xdotool", "windowfocus", "--sync", windows[-1]], env=env,
                   capture_output=True)
    return windows[-1]


def close_strays(window, env):
    """Puts away whatever the terminal opened in answer to a key it kept for
    itself — a help dialog, a menu — which can hold the keyboard even with the
    terminal focused. A click in the terminal, as a person would, and always:
    a GTK 4 popover takes no Escape without a window manager to give it the
    focus and autohides on a click outside, and one put away by the Escape
    leaves the keyboard on the button that opened it — ptyxis's menu does.
    Closed outright if the click did not do it."""
    def strays():
        found = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "."],
                               env=env, capture_output=True, text=True)
        return [w for w in found.stdout.split() if w != window]

    shape = subprocess.run(["xdotool", "getwindowgeometry", "--shell", window],
                           env=env, capture_output=True, text=True).stdout
    geometry = dict(line.split("=", 1) for line in shape.split() if "=" in line)
    x = int(geometry.get("X", 0)) + 10
    y = int(geometry.get("Y", 0)) + int(geometry.get("HEIGHT", 100)) - 10
    subprocess.run(["xdotool", "mousemove", str(x), str(y), "click", "1"], env=env,
                   capture_output=True)
    time.sleep(0.2)
    for stray in strays():
        subprocess.run(["xdotool", "windowclose", stray], env=env,
                       capture_output=True)


def read_line(fd, buffer, timeout):
    """One line off the FIFO, or None once `timeout` seconds have gone by."""
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


def package_version(program):
    found = subprocess.run(["rpm", "-q", "--qf", "%{VERSION}", "-f",
                            shutil.which(program) or program],
                           capture_output=True, text=True)
    return found.stdout.strip() if found.returncode == 0 else ""


def take(terminal, protocol, amberedit, out_dir, wait_ms, only):
    layer = LAYERS.get(terminal)
    program = layer[0] if layer else TERMINALS[terminal][0]
    version = package_version(program)
    label = f"{terminal}-{version}" if version else terminal
    if layer:
        label += f" in {TERMINALS[terminal][0]}-{package_version(TERMINALS[terminal][0])}"
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

    display = free_display()
    env = dict(os.environ, DISPLAY=f":{display}", QT_QPA_PLATFORM="xcb",
               LIBGL_ALWAYS_SOFTWARE="1", GDK_BACKEND="x11",
               GTK_A11Y="none", LANG="C.UTF-8")
    env.setdefault("XDG_RUNTIME_DIR", work)
    env.pop("WAYLAND_DISPLAY", None)
    xvfb = subprocess.Popen(["Xvfb", f":{display}", "-screen", "0", "1280x1024x24",
                             "-nolisten", "tcp"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)
    terminal_proc = None
    fd = None
    try:
        for _ in range(100):
            if os.path.exists(f"/tmp/.X11-unix/X{display}"):
                break
            time.sleep(0.05)
        conf = os.path.join(work, "layer.conf")
        with open(conf, "w") as f:
            f.write(layer[1] if layer else "")
        argv = [word.replace("{conf}", conf).replace("{label}", f"keytest{display}")
                for word in TERMINALS[terminal]] + [script]
        if terminal in NEEDS_DBUS:
            argv = ["dbus-run-session", "--"] + argv
        log = open(os.path.join(work, "terminal.log"), "w")
        terminal_proc = subprocess.Popen(argv, env=env, stdout=log, stderr=log,
                                         start_new_session=True)
        fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
        buffer = [b""]
        line = read_line(fd, buffer, 60)
        if line is None:
            with open(os.path.join(work, "terminal.log")) as f:
                tail = f.read()[-800:]
            return f"{stem}: never started\n{tail}"
        window = focus_terminal(env)
        codes = keycodes(env)
        x = Xtest(f":{display}")
        time.sleep(0.3)
        pressed = 0
        # What --keytest asks for, a line at a time, until it closes the FIFO.
        while line is not None:
            # Focus again before every key: one the terminal acts on itself —
            # F1 for its help — can open a window that takes the keyboard.
            if window is not None and line != "" and not line.startswith("done "):
                subprocess.run(["xdotool", "windowfocus", window], env=env,
                               capture_output=True)
            if line == "warmup":
                press("Space", codes, x)
            elif line == "flush" and window is not None:
                # A key the terminal kept for itself may have opened a popover
                # — ptyxis's menu on F10, tilix's sidebar on F12 — that holds
                # the keyboard. Escape puts most of them away and the click
                # the rest; --keytest throws out what they send.
                press("Esc", codes, x)
                close_strays(window, env)
            elif line.startswith("ready "):
                press(line[len("ready "):], codes, x)
                pressed += 1
            line = read_line(fd, buffer, wait_ms / 1000 + 10)
        return f"{stem}: {pressed} keys -> {out_path}"
    finally:
        if fd is not None:
            os.close(fd)
        if terminal_proc is not None:
            kill_group(terminal_proc)
        # Asked to go rather than killed, so that it takes its lock and socket
        # with it; killed only if it will not.
        xvfb.terminate()
        try:
            xvfb.wait(5)
        except subprocess.TimeoutExpired:
            kill_group(xvfb)
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amberedit", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--terminal", action="append")
    parser.add_argument("--protocol", action="append")
    parser.add_argument("--wait", type=int, default=1500,
                        help="how long a key is waited for, in ms")
    parser.add_argument("--only", help="comma-separated keys, for a quick run")
    parser.add_argument("--binary", action="append", default=[],
                        metavar="NAME=PATH",
                        help="run a terminal from PATH instead of PATH's lookup")
    args = parser.parse_args()
    for override in args.binary:
        name, _, path = override.partition("=")
        TERMINALS[name] = [path] + TERMINALS[name][1:]

    terminals = args.terminal or [t for t in TERMINALS
                                  if shutil.which(TERMINALS[t][0])]
    os.makedirs(args.out, exist_ok=True)
    for terminal in terminals:
        for protocol in args.protocol or PROTOCOLS:
            print(take(terminal, protocol, args.amberedit, args.out, args.wait,
                       args.only), flush=True)


if __name__ == "__main__":
    sys.exit(main())
