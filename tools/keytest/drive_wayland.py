#!/usr/bin/env python3
"""Takes `amberedit --keytest` in Wayland terminals without anybody at the keyboard.

The Wayland counterpart of drive_x11.py, for a terminal that has no X11 to be
driven through: each one is started inside a headless sway of its own, and the
keys it asks for are pressed with wtype through the virtual-keyboard protocol —
an event the terminal encodes itself, as XTEST is under X. sway is given a
config holding no binding at all, so no key stops at the compositor.

The binary has to be built with -DAMBEREDIT_KEYTEST=ON.

    drive_wayland.py --amberedit build/bin/amberedit --out testdata/keys \\
                     [--terminal foot ...] [--protocol "xterm kitty" ...]

Keys are named, synchronised and folded exactly as drive_x11.py does it, whose
keysym table this reads.
"""

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drive_x11 import PROTOCOLS, keysym, read_line  # noqa: E402

# How each terminal is told to run one program.
TERMINALS = {
    "foot": ["foot", "--"],
}

MODIFIERS = {"ctrl": "ctrl", "alt": "alt", "shift": "shift"}


def press(name, env):
    """Presses the key through wtype, every modifier held by hand. A capital is
    Shift with the letter, so that the terminal is handed a key press rather
    than a keysym somebody has already shifted."""
    mods, key = keysym(name)
    if len(key) == 1 and key.isalpha() and key.isupper():
        mods = mods + ["shift"]
        key = key.lower()
    # wtype hands the compositor a keymap of its own on every call, and a key
    # sent straight after it can reach the terminal before the keymap has —
    # and is lost. The pause comes first and the key after it.
    argv = ["wtype", "-s", "150"]
    for mod in mods:
        argv += ["-M", MODIFIERS[mod]]
    argv += ["-k", key]
    for mod in reversed(mods):
        argv += ["-m", MODIFIERS[mod]]
    subprocess.run(argv, env=env, capture_output=True)


def version_of(program):
    found = subprocess.run(["rpm", "-q", "--qf", "%{VERSION}", "-f",
                            shutil.which(program) or program],
                           capture_output=True, text=True)
    return found.stdout.strip() if found.returncode == 0 else ""


def take(terminal, protocol, amberedit, out_dir, wait_ms, only):
    version = version_of(TERMINALS[terminal][0])
    label = f"{terminal}-{version}" if version else terminal
    stem = f"{terminal}-{protocol.replace(' ', '-')}"
    out_path = os.path.abspath(os.path.join(out_dir, f"{stem}.txt"))

    work = tempfile.mkdtemp(prefix="keytest-")
    os.chmod(work, 0o700)
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

    config = os.path.join(work, "sway.cfg")
    with open(config, "w") as f:
        f.write("output HEADLESS-1 resolution 1280x1024\n")
        f.write("exec " + " ".join(shlex.quote(w) for w in TERMINALS[terminal] + [script])
                + "\n")

    env = dict(os.environ, XDG_RUNTIME_DIR=work, WLR_BACKENDS="headless",
               WLR_LIBINPUT_NO_DEVICES="1", WLR_RENDERER="pixman", LANG="C.UTF-8")
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "SWAYSOCK"):
        env.pop(name, None)
    sway = subprocess.Popen(["sway", "-c", config], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)
    fd = None
    try:
        fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
        buffer = [b""]
        line = read_line(fd, buffer, 60)
        if line is None:
            return f"{stem}: never started"
        sockets = [n for n in os.listdir(work) if n.startswith("wayland-")
                   and not n.endswith(".lock")]
        if not sockets:
            return f"{stem}: sway made no Wayland socket"
        env["WAYLAND_DISPLAY"] = sockets[0]
        pressed = 0
        # What --keytest asks for, a line at a time, until it closes the FIFO.
        while line is not None:
            if line == "warmup":
                press("Space", env)
            elif line == "flush":
                # Nothing the compositor opens, there being no binding; Escape
                # and Space put away whatever the terminal did.
                press("Esc", env)
                press("Space", env)
            elif line.startswith("ready "):
                press(line[len("ready "):], env)
                pressed += 1
            line = read_line(fd, buffer, wait_ms / 1000 + 10)
        return f"{stem}: {pressed} keys -> {out_path}"
    finally:
        if fd is not None:
            os.close(fd)
        sway.terminate()
        try:
            sway.wait(5)
        except subprocess.TimeoutExpired:
            sway.kill()
            sway.wait()
        time.sleep(0.2)
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--amberedit", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--terminal", action="append", choices=sorted(TERMINALS))
    parser.add_argument("--protocol", action="append")
    parser.add_argument("--wait", type=int, default=1000,
                        help="how long a key is waited for, in ms")
    parser.add_argument("--only", help="comma-separated keys, for a quick run")
    args = parser.parse_args()

    terminals = args.terminal or [t for t in TERMINALS if shutil.which(TERMINALS[t][0])]
    os.makedirs(args.out, exist_ok=True)
    for terminal in terminals:
        for protocol in args.protocol or PROTOCOLS:
            print(take(terminal, protocol, args.amberedit, args.out, args.wait,
                       args.only), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
