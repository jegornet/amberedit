#!/usr/bin/env python3
"""Folds the files drive_x11.py writes into one per way a terminal behaves.

The driver takes every terminal under each of the four modifier_key_reporting
values, and most terminals send the same bytes whichever was asked for: four
files saying one thing. This keeps one file per distinct set of bytes, its
header naming every protocol it stands for, and removes the rest.

    collapse.py DIR

A terminal whose four files agree becomes <terminal>.txt. One that sends two
ways becomes one file per way, named after the shortest protocol value among
the ones it stands for: <terminal>-kitty.txt and <terminal>-xterm.txt. The
comment column --keytest writes is dropped — it says what the binary that took
the file made of the bytes, which the test asks afresh every time.
"""

import collections
import os
import re
import sys

PROTOCOLS = ["xterm kitty", "kitty xterm", "kitty", "xterm"]
FOLDED = "# folded: one file per set of bytes the terminal sends"
SUFFIX = re.compile(r"^(?P<terminal>.+?)-(?P<protocol>xterm-kitty|kitty-xterm|kitty|xterm)\.txt$")


def body(path):
    """The key lines without their comments, and the header lines."""
    header, keys = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                header.append(line)
            else:
                keys.append(re.sub(r"\s*#.*$", "", line).rstrip())
    return header, keys


def main():
    directory = sys.argv[1]
    taken = collections.defaultdict(dict)
    for name in sorted(os.listdir(directory)):
        match = SUFFIX.match(name)
        # A file folded already says so — its name reads like a protocol of its
        # own, alacritty-kitty.txt — and is left as it stands.
        if match and FOLDED not in body(os.path.join(directory, name))[0]:
            protocol = match["protocol"].replace("-", " ")
            taken[match["terminal"]][protocol] = os.path.join(directory, name)

    for terminal, files in sorted(taken.items()):
        ways = collections.OrderedDict()
        headers = {}
        for protocol in PROTOCOLS:
            if protocol not in files:
                continue
            header, keys = body(files[protocol])
            ways.setdefault(tuple(keys), []).append(protocol)
            headers[tuple(keys)] = header

        written = set()
        for keys, protocols in ways.items():
            if len(ways) == 1:
                name = f"{terminal}.txt"
            else:
                name = f"{terminal}-{min(protocols, key=len).replace(' ', '-')}.txt"
            header = [re.sub(r"protocol: .*$", "protocol: " + " | ".join(protocols), line)
                      if "protocol: " in line else line
                      for line in headers[keys]] + [FOLDED]
            with open(os.path.join(directory, name), "w", encoding="utf-8") as f:
                f.write("\n".join(header + list(keys)) + "\n")
            written.add(name)
            print(f"{name}: {' | '.join(protocols)}")

        for path in files.values():
            if os.path.basename(path) not in written:
                os.remove(path)


if __name__ == "__main__":
    sys.exit(main())
