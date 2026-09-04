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
10, Fedora, Arch Linux, Debian stable, Ubuntu 22.04, 24.04 and 26.04, and tarballs for macOS
on both arm64 and x86_64.

Building it yourself needs CMake ≥ 3.16, a C++17 compiler, git, iconv, zlib,
tl::expected and the wide-character ncurses — [INSTALL.md](INSTALL.md) has the commands,
and what differs on macOS.

## Windows support?

Currently, only macOS and Linux. I don't have any Windows devices, so I can't say whether 
the project would build as-is (probably not) or whether Windows support could be implemented
(probably yes).

## Running

```bash
amberedit --setup                         # six steps, and you have a config
amberedit                                 # or: amberedit -c some/amberedit.cfg
```

**First run: `amberedit --setup`.** Six steps — who you are and which tosser
config you keep, where that file is, the charset your mail is read in and the one
it is written in, a nodelist if you have one, and where the config goes.

Run `amberedit --help` for more command line options.

### Keys

There is no help screen, so here they are.  Every key below that runs a command
can be moved — see [Rebinding the keys](#rebinding-the-keys)

**Ctrl-Q** quits from anywhere.

**Area list**

| Key | What it does                                                     |
|---|------------------------------------------------------------------|
| `↑` `↓` `PgUp` `PgDn` `Space`, `Home` `End` | move                                                             |
| `Enter`, `→`, click | open the area under the cursor                                   |
| any letter | search by area tag; `Backspace` edits the query, `Esc` closes it |
| `/` | go to the next area with unread messages                         |
| `Ctrl-U` | show only the areas with unread messages, or all of them again |
| `Ctrl-R` | rescan the message bases                                   |
| `Esc` | quit                                                             |

**Message list**

| Key | What it does |
|---|---|
| `↑` `↓` `PgUp` `PgDn` | move |
| `Home`, `End` | first, last |
| `Enter`, `→`, click | open the message |
| `t`, `Space` | mark the message under the cursor, or take the mark off it |
| any digit | go to a message by number: the title's `12/44` becomes a field standing in exactly those columns, `Enter` opens that message — or puts the cursor on its row with `msglist_goto_field_opens off` — `Backspace` edits it, `Esc` closes it |
| `Esc` `←` `Backspace` | back to the area list |

**Reader**

| Key | What it does                                                                 |
|---|------------------------------------------------------------------------------|
| `←` `→` | previous, next message — off either end leaves the area, or → goes on to the next unread area (`reader_edge`) |
| click on the side of the text | the same two, from the pointer: the columns down either side of the message are ← and → (`reader_side_taps`, `reader_side_tap_width`) |
| `↑` `↓` `PgUp` `PgDn` `Space` `Shift+Space` | scroll the message                                                           |
| `Home`, `End` | top, bottom                                                                  |
| any digit | go to a message by number: the title's `12/44` becomes a field standing in exactly those columns, `Enter` goes there, `Backspace` edits it, `Esc` closes it |
| `q` / `F4` | reply                                                                        |
| `e` | write a new message                                                          |
| `n` / `F5` | reply into another area                                                      |
| `Alt-Q` | reply, addressed to whoever the message was written to                       |
| `m` | forward, move or copy into another area — with messages marked, asks whether you mean those (copy/move) or this one |
| `c` / `F2` | change the message                                                           |
| `d` / `Del` | delete it (asks first) — with messages marked, asks whether you mean those or this one |
| `-` `+` / `=` | up and down the thread                                                       |
| `Ctrl-F` / `F6` | look for a message in the area                                               |
| `l` / `F9` | the list of messages                                                         |
| `k` | show the kludges — a reply then quotes them, a forward carries them          |
| `b` | toggle the scrollbar                                                         |
| `t` | mark the message, or take the mark off it                                    |
| `s` | mark a run of messages at once: all, none, inverted, or everything before or after this one |
| `Ctrl-X` | run your own shell — leaving it comes straight back to the message      |
| `i` | technical info from the message base                                         |
| `w` / `F7` | export the message to a text file or decode UUE if any — with messages marked, asks whether you mean those or this one |
| `Ctrl-N` / `F10` | the nodelist                                                                 |
| `Space` | show a message blanked as a twit                                             |
| `Esc` `Backspace` | back to the area list                                                        |

**Editor**

| Key | What it does                                                       |
|---|--------------------------------------------------------------------|
| `Tab` `Shift-Tab` | the ring: the fields, the subject, the attributes button, the text |
| `Enter` | the next field, then down into the text                            |
| `Alt-H` | edit header                                                         |
| `Ctrl-S` / `F2` | save (asks first)                                                  |
| `Esc` | drop the message (asks first)                                      |
| `Ctrl-F` | edit message attributes                                         |
| `Ctrl-O` / `F3` | open a file to insert as text or uuencoded             |
| `Ctrl-Y` | delete the line — or the button beside it, on the right-hand edge   |
| `Ctrl-U` | put back the last line `Ctrl-Y` took, above the cursor              |
| `Ctrl-D` | delete the quoted text after the cursor                            |
| `Ctrl-W` / `Alt-Backspace` | delete the word before the cursor                     |
| `Alt-B` `Alt-F` / `Alt+←` `Alt+→` | by words                                         |
| `Ctrl-A` `Ctrl-E` | to the start of the line and to its end, as `Home` and `End`     |

### Rebinding the keys

A `keys` line names a layout:

```
keys ~/ftn/etc/amberkeys.cfg
```

A line of that file is a key and a command:

```
l    reader.list
F2   reader.change
```

A `keys_mode` line says what that file does to the layout AmberEdit already
has — `merge` by default:

**`keys_mode merge` reads the file on top of the standard layout.** A command the file
does not name keeps the key it has, and a key the file gives to something else
is taken off whatever had it: a clash is settled for the file. So a file of
three lines says what three keys do and changes nothing else.

**`keys_mode clear` throws the standard layout away first, and the file is then
the entire layout.** Any command not listed in it has no keybinding at all, so
copy `amberkeys.cfg.example` (which contains the default bindings, spelled out)
and edit that instead of starting from a blank file.

Keys are written as a single character (`l`, `G`, `/`, `+` — case tells two
apart), a function key (`F1` to `F12`), `Del`, `Ctrl-` and a letter, or `Alt-`
and a letter, an arrow, a function key or `Backspace`. Two screens may share a key, as `F2`
does between the reader and the editor; two commands of one screen may not, and
a layout that tries says which line clashes with which.

**Moving about is not bindable**: the arrows, `PgUp` and `PgDn`, `Home` and
`End`, `Space`, `Enter`, `Esc`, `Backspace` and `Tab` mean the same thing on
every screen — bare, that is: `Alt-Left` and `Alt-Backspace` are chords of their
own and may be bound — and the dialogs answer for themselves entirely. `Space` in
the message list marks the message rather than paging, which is a key the screen
answers and not a binding; `PgDn` pages there as everywhere.

The commands are `app.quit`; `arealist.` `next_unread`, `toggle_unread` and
`rescan`;
`msglist.mark_toggle`;
`reader.` `reply`, `reply_elsewhere`, `comment_reply`, `new`, `forward`, `change`,
`delete`, `export`, `find`, `list`, `info`, `nodelist`, `kludges`, `scrollbar`,
`mark_toggle`, `mark_menu`,
`shell`, `thread_up` and `thread_down`; and `compose.` `save`, `attributes`, `import`,
`header_back`, `delete_line`, `restore_line`, `delete_quote`, `delete_word`,
`word_left`, `word_right`, `line_start` and `line_end`. Every one of the three
screens also has `extern_util0` through `extern_util9`, for the programs
`amberedit.cfg` names — `Alt-F1 reader.extern_util0`.

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
in the order they stand in the area. The uuencoded files a message carries are
asked about first, and are looked for **in the message on screen alone** — so a
message carrying one still offers Files or Text before anything else. Answer that
`Text`, or read a message carrying no file, and the scope box follows, then the
usual file picker. Files and a marked set never meet: those files came out of one
message.

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

### The nodelist

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
A long list lives in a file of its own: `twit @file:twit.list`. See
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

This works straight out of `cmake --build` as well — the build's own catalogs are
compiled in ahead of the installed ones, so `LANG=ru_RU.UTF-8 ./build/bin/amberedit`
is Russian before anything has been installed.

One thing is the system's rather than AmberEdit's: **the locale you ask for has
to exist**. A stock Debian or Ubuntu generates none at all, and gettext will not
translate under `C`:

```bash
sudo apt install locales && sudo locale-gen ru_RU.UTF-8
```

`locale -a` lists what a system already has. AmberEdit says so on startup when
the language you asked for is one it has and the system cannot install, and runs
in English meanwhile.

Russian ships with it. To add a language, or to fix a word in one:

```bash
cmake --build build --target pot        # refresh po/amberedit.pot from the source
msginit --input=po/amberedit.pot --locale=de --output=po/de.po
cmake -S . -B build && cmake --build build
```

`msgfmt` from gettext is what compiles them; a build without it is a build with
the English. `cmake --build build --target update-po` merges a refreshed
`amberedit.pot` into the translations that are already there.

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

## License

GPL-2.0-or-later — see [LICENSE](LICENSE).
