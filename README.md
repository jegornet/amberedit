```
,d$$$$b.              db                         ,@$$$$@b       db QO   db
$$'  `$$              $$                         @$             $$      $$
$8    8$ ,88@q.,p@88. $$d%@8%. ,o@$$@@. ,d@$$@q. $b       ,o@$$$$$ && d#@@%D
$$8@@8$$ $$   $$   $$ $$'  `@$ $@    @8 $$'  `8' $$$0f    $$'  `$$ $$   $$
$$    $$ $$   $$   $$ $$    B$ $$$$$B@' $$       $$       $$    $$ $$   $$
$$    $$ $$   $$   $$ $$.  ,@B $$.      $$       @$       Q@.  ,$@ $$   $@.,J@
$$    $$ $$   $$   $$ `Q$$$$@' `@$$$$Q: $$       `@$$$$@% `Q@$$@@' $$   `@$$0'
```

[![CI](https://github.com/jegornet/amberedit/actions/workflows/ci.yml/badge.svg)](https://github.com/jegornet/amberedit/actions/workflows/ci.yml)

A terminal-based (TUI) [FidoNet](https://www.fidonet.org/) mail editor for Linux and macOS.

<img src="amberedit_demo.gif" alt="demo" width="400" />

## Features

- **Mouse/taps support and adaptive UI**: suitable to read FTN mail from any device, 
  be it a desktop or a mobile SSH client
- **Charsets encode/decode**: supported UTF-8 and legacy charsets like CP866,
  CP437, LATIN-1 etc. UTF-8 terminal locale is recommended.
- **Squish, JAM and Fido `*.msg`** message base format, with the areas taken from
  your tosser's config (fidoconfig, `areas.bbs` or `squish.cfg`). You can also configure
  the areas manually using `area … endarea` blocks.
- **Reading and writing**: reply, reply into another area, forward, move or copy 
  a message elsewhere, modify or delete. Carbon copies and crossposts
  (`CC`/`XC`/`XP` lines like in GoldED). GoldED-style templates, and an origin or
  tearline picked at random from a file of them. And `twit` keywords
  to ignore those names or subjects you won't read.
- **Text and UUE import/export**: import a text file into a message, insert a uuencoded 
  binary file, write a message out to a text file, or extract uuencoded files from a message.
- **Nodelists and pointlists**, including ZIP-archived — automatically compiled at startup
   when they change
- **Echolists**, compiled at startup on the same terms
- **A setup wizard**: `amberedit --setup` asks what a first config has to say and
  writes one, so you don't have to edit it by hand before the first start
- **Color Themes** in the terminal's own 256 colors
- **Russian and English UI**: the words on the screen come from a gettext
  catalog the config names by path, so adding a language is a `.po` file. Let me know which
  languages you'd like to add.
- **ANSI graphics** and **Renegade/Telegard BBS color codes** support — experimental,
  turned on per echo area rather than for all your mail (see *Experimental options*
  at the end of `amberedit.cfg.example`).
<details>
<summary>ANSI graphics demo</summary>
<img src="amberedit_demo_ansi.webp" alt="ansi demo" />
</details>

## Building and installing

Every tagged release carries built packages —
[Releases](https://github.com/jegornet/amberedit/releases) has prebuilt packages for RHEL 8, 9,
10, Fedora, Arch Linux, Debian stable, Ubuntu 22.04, 24.04 and 26.04, tarballs for macOS
on both arm64 and x86_64, and a zip for Windows on x86_64.

Building it yourself needs CMake ≥ 3.16, a C++17 compiler, git, iconv, zlib,
tl::expected and the wide-character ncurses — [INSTALL.md](INSTALL.md) has the commands,
and what differs on macOS and Windows.

## Windows

**Windows 10 or later, x64**. The zip holds one executable and the files it
reads; there are no DLLs to put beside it and nothing to install.

Older versions of Windows are not supported. AmberEdit is built against the Universal
C Runtime, which those releases do not carry.

The Windows build draws through
[PDCursesMod](https://github.com/Bill-Gray/PDCursesMod) where the others draw
through ncurses. Everything else is the same code.

## Running

```bash
amberedit --setup                         # six steps, and you have a config
amberedit                                 # or: amberedit -c some/amberedit.cfg
```

**First run: `amberedit --setup`.** Six steps — who you are and which tosser
config you keep, where that file is, the charset your mail is read in and the one
it is written in, a nodelist if you have one, and where the config goes.

Run `amberedit --help` for more command line options.

### Configuration

Without `-c` the config is looked for in `$AMBEREDIT_CONFIG`, `./amberedit.cfg`
and `~/.ambereditrc`, in that order. Every setting, what it takes and what it
defaults to, is in `amberedit.cfg.example`. Also, `amberedit --setup` writes
a config out of that file for you.

A `group … endgroup` block states settings for the echoes its `member` patterns
match — the charsets, the origin, the template, the twits and the rest of what is
per-echo rather than per-config:

```
group
  member fsx_ads
  bbs_codes_ansi on
endgroup
```

### Keys

**F1** opens the key list of whichever screen is up, each with what it does beside
it — the layout read back, so a rebound key is the key the box shows.
[KEYS.md](KEYS.md) is the same thing written out, screen by screen.

**Ctrl-Q** quits from anywhere.

### Rebinding the keys

Every key that runs a command can be moved: a `keys` line in `amberedit.cfg`
names a layout of your own, and `keys_mode` says whether it is read on top of
the standard one or in place of it. [KEYS_REBINDING.md](KEYS_REBINDING.md) is
how such a file is written, and which commands there are to bind.

### Finding a message

**Ctrl-F (or F6) in the reader** asks what to look for and where — in the header
and the text, or in the header alone. The header consists of the From and To names,
the addresses under them and the subject. The search runs from the message
on screen to the end of the area; the same words looked for again go on from
the message after the one that was found, so Ctrl-F, Enter, Ctrl-F, Enter walks
from occurrence to occurrence. It stops at the end of the area, no wrapping round
to the front.

The message found opens scrolled to the occurrence, with every occurrence in it —
in the body and in the header block alike — on the theme's `found` fill. Moving
to another message takes the highlight off.

**Case is folded by the charset the message declares** — its `CHRS` kludge, or
the area's `default_charset` where it carries none — rather than by the locale.
**CP866 carries the Russian language support quirks** with it: a message written
in it may spell Н, р and у with the Latin H, p and y, so those pairs are folded
together — and only there, since in a western area they are six different letters.

### Marking messages

**`t` in the reader or in the message list** marks the message, and `t` again
takes the mark off. `Space` says the same thing in the list. A marked message
wears a `*` beside its number in the list and after the `111/111` in the reader's
top line.

**`s` in the reader** opens a box for marking a run at once: every message in the
area, no message at all, the marks turned inside out, everything after the
message you are on, or everything before it. The last two add to what is already
marked rather than replacing it.

**`d` deletes the marked messages, `m` copies or moves them, and `w` writes them
out.** With nothing marked the three keys ask what they always asked. With a set
standing each asks which messages you mean first: Marked, Current or Cancel.

For `d` that box is the confirmation, so answering it deletes. The reader stays
on the message it was showing, or on the nearest one before it where that was
among the ones that went.

For `m` it is the first of three boxes. Answer it `Current` and the rest is
exactly what `m` has always done. Answer it `Marked` and the next box offers
**Copy and Move only** — forwarding is writing a message of your own with another
quoted in it, and there is no one message a whole set could go into — and then
the area picker as usual. Copy leaves the marks standing, the messages being
still where they were; Move takes them out of the area and the marks with them.

**`w` (or `F7`) writes the marked messages out into one file**, one after another
in the order they stand in the area. 

Marks are yours for as long as the area is open — nothing is written to the
message base, and leaving the area forgets them.

The `*` is the `m` field of `msglist_format`, which the default formats put where
the blank between the number and the first name used to stand: a list with
nothing marked in it looks exactly as it always did. Take `m` out of your own
format and the list simply shows no marks.

### Carbon copies and crossposts

Two commands you write into the message itself, same as in GoldED+. Each has
to begin its line, and neither is read in a quoted line:

```
CC: Ivan Ivanov, 2:5020/1234, #Vasya Pupkin, @friends.lst
XC: ru.* ru.linux, #ru.talk
```

`CC:` sends a copy of the message to everybody it names — a name the nodelist
knows, an address whole or in part (`/1234` for a node in your own net), an
`address_macro`, or a file of names — and `XC:`/`XP:` posts it in every echo its
masks name. A `#` makes the copy without naming its recipient in the message.
You are asked before either is carried out, and a name nobody could find leaves
its line where you wrote it rather than losing what you asked for.

Copies of a message written in an echo area go to the netmail area that echo's
`reply_to_area` names; what the message keeps in place of the commands is
`compose_cc_list` and `compose_xc_list`. See `amberedit.cfg.example`.

### Nodelists and pointlists

The nodelist filename may be a wildcard — `nodelist ~/ftn/nodelist/z2daily.999`
or `z2daily.*` takes the newest file it matches. You can also specify ZIP-packed
nodelists like `nodelist ~/ftn/nodelist/NODELIST.ZIP` or 
`nodelist ~/ftn/nodelist/nodelist.z99` or `nodelist.z*`.

You can specify the nodelist's charset (`nodelist ~/ftn/nodelist/nodelist.ndl
UTF-8`), which is what a nodelist carrying names and locations outside ASCII
needs; a line that states none is read in the charset your locale names.

**Ctrl-N (or F10) in the reader** opens the nodelist on whoever wrote the message
on screen. The Lookup line takes an address, whole or in part — `2`, `2:382`,
`2:382/736` — or any part of a sysop's name, and Enter walks through everything
it finds. The list is the whole nodelist either way: a node is worth as much
for its neighbors as for itself.

**Writing netmail**, Enter on a half-filled To row asks the nodelist for the
other half: a name with no address under it lists the nodes of that name,
closest first, and an address with no name above it shows the nodelist at that
address. Part of a name is enough — Enter on a row addresses the message to that
node, name and address both, with the name spelled as the nodelist spells it,
and moves on to the subject.

**The header block** in the reader shows where the message was written, controlled
by the `show_location` config setting. A point that isn't listed in any pointlist
is placed at its parent's ("boss's") location — which is also the default view
the nodelist box opens to.

The nodelists themselves are compiled at startup whenever they have changed, so
there is nothing to run by hand; `amberedit --compile` compiles them anyway.
Which files, under what patterns, and where the compiled one goes — see
`amberedit.cfg.example`.

### Echolists

An echolist says what an echo is — the tag and a description, agreed across the
network rather than written into your tosser config. Point `echolist` at the
lists you keep, one line each, either a `.lst`/`.na` or a zip archive of them,
and the descriptions turn up in the area list's description column and in the
`@CDESC`/`@ODESC` template tokens.

The filename may be a wildcard — `echolist ~/ftn/echolist/echo*.zip` takes the
newest file it matches, which is what to write when the list you are sent carries
its month in its name.

You can specify the echolist's charset (`echolist ~/ftn/echolist/echo50.lst CP866`).

`arealist_description_priority` says which description an echo with two of them
is shown by. As with the nodelists, they are compiled at startup whenever they
have changed and there is nothing to run by hand. See `amberedit.cfg.example`.

### Twits

Whom you would rather not read: names, FTN address patterns and subjects, listed
globally or inside a `group … endgroup` block, and `twit_mode` says what becomes
of one — from a notice in place of the text to deleting it as the area opens.
A long list can live in a file of its own: `twit @file:twit.list`. See
`amberedit.cfg.example`.

### Themes

A color in a theme is a number from 0 to 255 — an entry in
[256-color palette](https://www.ditig.com/256-colors-cheat-sheet). Entries 0–15 are the
colors you have already chosen for everything else you run.

`themes/black.cfg` is the built-in palette written out — what AmberEdit draws
with when the config names no theme, and the file to copy and edit.
`themes/16_colors.cfg` uses nothing above 15, you might want to set it if you prefer
customizing the pallete in your terminal app.
Also, we have `themes/blue.cfg` and `themes/white.cfg`

### Language

The language is the environment's, as it is for every other program on the
system — `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`, in that order of
preference. Nothing goes in the config:

```bash
LANG=ru_RU.UTF-8 amberedit          # Russian
LANGUAGE=ru amberedit               # Russian, leaving the rest of the locale alone
amberedit                           # whatever your shell already says
```

## License

GPL-2.0-or-later — see [LICENSE](LICENSE).
