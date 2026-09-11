# A truecolor theme in PuTTY

PuTTY draws a truecolor theme — `themes/truecolor_bg_night.cfg`, or any theme of
your own written in six-hex-digit colors — quantised to the nearest entry of the
256-color palette. Windows Terminal and most other modern terminals draw the
same theme exactly.

This is fixed by giving the machine AmberEdit runs on a terminfo entry PuTTY can
read, and then naming it in `TERM`.

## What to do

You need PuTTY 0.71 or newer, and a `tic` from ncurses 6.1 or newer on the
machine AmberEdit runs on.

**1. Compile the terminfo entry.** On the machine AmberEdit runs on — the remote
one, if you reach it over SSH:

```
cat > /tmp/putty-direct.ti <<'EOF'
xterm-direct-semi|xterm 24-bit direct color, semicolon form,
	RGB,
	ccc@, initc@,
	colors#0x1000000, pairs#0x10000,
	setaf=\E[38;2;%p1%{65536}%/%d;%p1%{256}%/%{255}%&%d;%p1%{255}%&%dm,
	setab=\E[48;2;%p1%{65536}%/%d;%p1%{256}%/%{255}%&%d;%p1%{255}%&%dm,
	op=\E[39;49m,
	use=xterm-256color,
EOF
tic -x -o ~/.terminfo /tmp/putty-direct.ti
```

`tic` compiles the description into the binary form ncurses reads, and
`-o ~/.terminfo` keeps it in your home directory rather than the system one, so
no root is needed. The entry is `xterm-256color` with one thing changed: how a
24-bit color is spelled on the way out.

**2. Check that it took.**

```
TERM=xterm-direct-semi tput colors
```

This must print `16777216`. If it prints `32767`, your `tic` is older than
ncurses 6.1 and truncated the number; AmberEdit will read the terminal as an
ordinary 256-color one and nothing will have changed.

**3. Check that PuTTY understands 24-bit color at all.**

```
printf '\033[38;2;255;0;0mRED\033[0m\n'
```

If `RED` is not red, your PuTTY is older than 0.71 and has no truecolor support.
Stop here: no terminfo entry can add it. Use a 256-color theme instead.

**4. Run AmberEdit with that `TERM`.**

```
TERM=xterm-direct-semi amberedit
```

To keep it, either set PuTTY's **Connection → Data → Terminal-type string** to
`xterm-direct-semi`, which applies it to everything in the session, or leave
`TERM` alone and put an alias in your shell's rc file:

```
alias amberedit='TERM=xterm-direct-semi amberedit'
```

The alias is the conservative choice: a direct-color `TERM` reports sixteen
million colors to every program, and an old one may not expect that.

## Why it is needed

A truecolor theme reaches a terminal by one of two routes. As the first one fails,
this entry opens the second.

**The palette route.** A terminal drawing a truecolor theme has no color number
to use, so AmberEdit lends it one palette entry per color the theme asks for and
redefines that entry to the color — but only where the terminal says through
terminfo that it accepts redefinitions. PuTTY seems to ignore them. There is
no way to ask a terminal whether a redefinition landed, so this cannot be
detected and worked around; what AmberEdit does instead is choose the entry that
is *already nearest* the color asked for, which costs an honest terminal nothing
and leaves PuTTY drawing the theme quantised rather than a screen of arbitrary
colors.

**The direct-color route.** A `TERM` ending in `-direct` puts the terminal in a
mode where a color is sent as the triple itself and the palette is not involved,
which is what the entry above selects. The stock `xterm-direct` does not work
here, because it spells the triple with colons — `ESC[38:2::255:0:0m` — and PuTTY
parses only the semicolon spelling. It discards the whole sequence, which is why
`TERM=xterm-direct` gives a monochrome screen rather than a quantised one.
`xterm-direct-semi` differs from `xterm-direct` in exactly that: `setaf` and
`setab` write `ESC[38;2;255;0;0m`.

The rest of the entry follows from the two routes. `colors#0x1000000` is what
puts AmberEdit on the direct route at all — it reads a terminal claiming more
than 16,777,215 colors as one that takes triples. `ccc@` and `initc@` withdraw
the promise about palette redefinition, which is neither used nor true here.
`RGB` is the standard flag marking a direct-color entry.

## Other terminals

Any terminal that reports palette redefinition through terminfo and ignores it
behaves the same way, and the same entry fixes it the same way. To tell whether
yours is one of them:

```
printf '\033]4;200;rgb:ff/00/00\033\\'; printf '\033[48;5;200m    \033[0m\n'
```

A terminal that honours the redefinition paints that block red. One that does
not leaves it the pink that entry 200 already was, and will draw a truecolor
theme quantised.
