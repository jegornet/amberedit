# AGENTS.md

Working notes for anyone — human or agent — changing this repository.

**What each file answers.** The user-facing meaning of a setting — what it does,
what values it takes, what it defaults to — is written once, in
`amberedit.cfg.example`. This file says only what the code has to guarantee
about it: where it is read, where it is applied, and what breaks if that changes.

**Every document here describes the project as it stands.** README.md, INSTALL.md,
`amberedit.cfg.example`, this file and the comments in the code all say what is
true now, in the present tense. What was tried and taken out, what a setting used
to be called, which approach lost — none of it is recorded: it is one more thing
to keep in step with the code, and a reader who has to work out which paragraphs
are still true has been given work rather than answers. When something changes,
rewrite the sentence rather than adding "previously…" beside it, and delete what
has stopped being true. History belongs in a changelog, and there is no such file
in this repository yet; if one is added, it is the single exception to this rule
and nothing leaks back into the documents above.

The rule is about *narrative*, not about reasons. A rule that keeps the next
change from undoing a deliberate decision stays — written as what holds now and
why, never as the story of what happened: "horizontal scrolling is deliberately
unhandled, because nothing the terminal reports carries an event's phase" and not
"we tried swipe-to-next-message and removed it".

Contents: [What AmberEdit is](#what-amberedit-is) ·
[Build and test](#build-and-test) · [Packaging](#packaging) ·
[Layering](#layering) ·
[Code conventions](#code-conventions) · [Domain notes](#domain-notes) ·
[The message base drivers](#the-message-base-drivers) ·
[The nodelist](#the-nodelist) · [The echolist](#the-echolist) ·
[The keyboard](#the-keyboard) ·
[Reference material in the tree](#reference-material-in-the-tree) ·
[Current scope](#current-scope)

## What AmberEdit is

A TUI Fidonet mail editor. It reads message bases (Squish, JAM, Fido `*.msg`)
on top of an existing tosser configuration and does not duplicate its settings.
It reads and writes message bases, but it is not a mailer and not a tosser: what
it writes goes out only when a tosser carries it.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/bin/amberedit_tests          # or: ctest --test-dir build
```

Both must be clean before anything is called done, and the whole thing must
build from a fresh clone with no network. The build produces one binary; the
external inputs are the wide ncurses, iconv, zlib — for the zipped nodelists and
echolists AmberEdit unpacks itself — tl::expected, which every fallible operation
answers with, and doctest for the tests, all of them found on the system and none
of them fetched.

Test cases carry their tags inside the name — `TEST_CASE("... [nodelist][ui]")` —
because doctest's `TEST_CASE` takes a name and nothing else. They are filtered
with `-tc`, which matches the whole name by wildcard:
`./build/bin/amberedit_tests -tc="*[squish]*"`.

The code is C++17 and wants GCC 8, Clang 7 or Apple Clang 11 at the least;
`CMakeLists.txt` refuses anything older by name, because an older compiler
accepts `-std=c++17` well enough for CMake to call `CMAKE_CXX_STANDARD 17`
satisfied and then buries the build in errors from inside `<bits/>`.

**That floor is the point of the standard, not an accident of it.** RHEL 8 and
its rebuilds must build AmberEdit out of the box, with the stock GCC 8.5 and the
CMake 3.20–3.26 the release ships — no `gcc-toolset`, no newer CMake. Worth
checking after anything that touches the build or a header, because a Mac's
Clang accepts most of C++20 under `-std=c++17` with only a warning:

```bash
docker run --rm -v "$PWD":/src:ro rockylinux:8 sh -c '
  dnf -y install gcc-c++ cmake git ncurses-devel zlib-devel &&
  cp -a /src /work && rm -rf /work/build &&
  cmake -S /work -B /out && cmake --build /out -j"$(nproc)" &&
  cd /out && ctest --output-on-failure'
```

The copy is not incidental: one test opens `testdata/msgbase/charsets` in the
source tree itself, so a read-only `/src` fails there and nowhere else. Short of
a container, `-Werror=c++20-extensions` on a Clang build catches the language
half of the same thing.

**CI runs that same command, on every push.** `.github/workflows/ci.yml` walks
Rocky 8 and 9, Fedora, Debian stable and Ubuntu 22.04 and 24.04 this way, plus
macOS on both architectures. Two things about it are decisions rather than
detail:

- The Linux jobs run their distribution under `docker run` from an ordinary
  `ubuntu-latest`, **not** through Actions' `container:` key. `container:` makes
  every action run its own Node inside the image, and RHEL 8's glibc sits exactly
  on the boundary of what current Node builds accept; checking out on the host
  and mounting the tree in has no such question in it, and it is this command.
- The macOS jobs are the only coverage two branches of `CMakeLists.txt` get at
  all: `find_library(ICONV_LIBRARY ...)`, because glibc has iconv in libc, and
  the `<ncursesw/curses.h>` spelling. Do not drop them for being slow.

## Packaging

Three formats, and one rule holding them together: **`cmake --install` places
everything a package ships** — the binary, `default.tpl` and `themes/*.cfg`, the
last two under `${CMAKE_INSTALL_DATADIR}/amberedit`, which is the path
`amberedit.cfg.example` names. No package recipe places a data file of its own,
because a second copy of those paths is a second thing to keep in step with the
sample config. Only documentation is each format's own: the README and the two
example configs go through `%doc`, `debian/amberedit.docs`, or the top of an
archive.

- `amberedit.spec` — RPM, built for Rocky 8 and 9 and Fedora. It carries one
  dependency nothing can work out on its own: **`glibc-gconv-extra` on RHEL 9 and
  later and on Fedora**, where CP866, CP437 and KOI8-R were split out of the base
  glibc. A gconv module is opened by name at run time, so there is no linkage for
  rpm to find and no configure-time probe that would help — the charsets are
  simply missing at run time and messages read as mojibake. RHEL 8 is the last
  release with them in libc, so the dependency is conditional; Debian, Ubuntu and
  macOS all carry the full set. The CI and release matrices install it too, or
  `%check` fails three charset tests and nothing says why.
- `debian/` — deb, built for Debian stable and Ubuntu 22.04 and 24.04. `dh` runs
  the tests as part of the build; `doctest-dev` is the build dependency and is
  spelled the same on all four. `libexpected-dev` is in *universe* on jammy and
  noble — the official images enable it and a minimal chroot does not.
- `.github/workflows/release.yml` — everything a `v*` tag produces. It checks the
  tag against `project(AmberEdit VERSION ...)` before building anything: a tag
  disagreeing with the source is a release nobody can rebuild.

So C++20 does not go back in. The three corners that keep wanting to:
`std::ranges::` algorithms (use the iterator-pair `std::` ones), `starts_with` /
`ends_with` (use `config::text::startsWith`; `domain/` has its own copy in
`message.cpp` so that it goes on including only `domain/`), and `contains` on a
map or set (use `count(k) != 0`). `.clang-tidy` leaves out the three checks that
would argue for the C++20 spellings for exactly that reason.

Four things in the build follow from the same floor:

- GCC 8 keeps `std::filesystem` in a separate `libstdc++fs`. `CMakeLists.txt`
  finds out by linking, not by version, and puts the result in the
  `amberedit_filesystem` interface target that `amberedit_core` exports.
- **`Result<T>` is `tl::expected` and not `std::expected`, and that too is the
  floor talking.** `std::expected` is C++23 and GCC 8 has no part of it, so the
  library is the only way the project gets the type at all — and it is packaged
  on every target under one header and one CMake package: `expected-devel` on
  EPEL 8 and 9 and Fedora, `libexpected-dev` on bookworm, trixie, jammy and
  noble, `tl-expected` in Homebrew. **What may be used of it is the 1.0.0 API
  and no more**, because bookworm and jammy carry 1.0.0: `has_value()`,
  `operator bool`, `operator*`, `error()`, `value_or()` and `tl::make_unexpected`.
  Not `transform`, not `transform_error`, and not the deduced `tl::unexpected` —
  all three are newer, all three compile on Fedora and on a developer's Mac, and
  all three fail the two oldest debs. `support/result.hpp` says the same thing
  where the type is declared.
- **The tests are on doctest, and that is a packaging decision rather than a
  taste.** Nothing is fetched, so a package build — which has no network — gets
  the tests too, and doctest is the only framework every target packages under
  one API: `doctest-devel` on EPEL 8 and 9 and Fedora, `doctest-dev` on bookworm,
  trixie, jammy and noble, `doctest` in Homebrew, 2.4.8 through 2.5.x and source-
  compatible across all of them. Catch2 has no such version — EPEL 8 carries only
  the 2.13 series and trixie, noble and Homebrew carry only v3, and the two are
  not source-compatible — so it would need a compatibility header forever.

  What doctest does not have is matchers. A substring assertion is therefore an
  ordinary predicate, `amberedit::test::contains` from `tests/test_strings.hpp`,
  paired with `CHECK_MESSAGE` so a failure still prints the string that was
  searched. `CHECK_THROWS_WITH` compares a whole message and nothing less, which
  none of these assertions want, so `errorFrom` from the same header catches the
  message and `contains` is put to it.

- **Never give a type a free `toString()`, and this is a build error rather than
  a preference.** To print what a failing `CHECK(a == b)` compared, doctest calls
  an unqualified `toString(a)`, and ADL hands it any free function of that name
  in the type's own namespace. doctest 2.4 needs one returning `doctest::String`
  and gets `std::string`, so the test file will not compile; 2.5 wraps the
  result and compiles fine. Since Fedora and Homebrew are on 2.5 and EPEL,
  Debian and Ubuntu are all on 2.4, that is a break which passes on a developer's
  Mac and fails every packaging platform. `domain::nameOf(MsgBaseType)` and
  `domain::nameOf(AreaKind)` are so named for this reason and no other — the
  spelling matches `ui::nameOf(KeyCommand)`, which came first. A member
  `.toString()`, as FtnAddress and AddressPattern have, is not found by ADL and
  is not affected.

To exercise the app itself against the checked-in base:

```bash
cp amberedit.cfg.example amberedit.cfg       # point tosser_config at a real areas file
./build/bin/amberedit -c amberedit.cfg
```

`testdata/msgbase/localnet.*` is a real Squish base with 43 messages, some of
them in CP866 Cyrillic — the quickest way to see whether decoding still works.
Copy it somewhere writable first if anything is to be written into it.
`amberedit.cfg.example` is itself read by a test (`tests/config/app_config_test.cpp`),
so a setting renamed in the code and not there fails the build.

The version lives in `project(AmberEdit VERSION ...)` in `CMakeLists.txt` and
nowhere else **that the code can see**: `src/version.hpp.in` is configured into
`build/generated/version.hpp`, giving `kProgramName` ("AmberEdit"), `kSystemName`
(`CMAKE_SYSTEM_NAME` lowercased), `kLongProgramName` ("AmberEdit/darwin"),
`kVersion` and `kProgramId`. `--version` prints `kProgramId`; the template tokens
are `@pid` (the bare name), `@longpid` (the name with the system under it) and
`@ver`/`@rev`/`@version` (the number), which is how the default tearline
`@longpid @version` reaches "AmberEdit/darwin 0.1" with the version standing
only in `CMakeLists.txt`. The two packaging files state it again — `Version:` in
`amberedit.spec` and the first line of `debian/changelog` — because neither rpm
nor dpkg will read it out of a CMake file. Nothing generates those from it, so
they are checked instead: the release workflow compares all three against the tag
before it builds anything, a deb being the one that would otherwise fail in
silence and ship the old number. The tests build their expected tearline from the same
constants. The tearline and origin *texts* are the user's: `tearline` and
`origin` are expanded as template lines (`expandTokens` in `app/msg_template`)
and closed round by `message_builder` — `"--- " + tearline` and
`" * Origin: " + origin + " (addr)"`.

## Layering

```
UI (app_shell + the screens, drawn by ui/term on ncurses)
   ↓
Application (AreaManager, Navigator)
   ↓
Domain (Area, Message, FtnAddress) + Ports (IMsgBase, ILastReadStore, IAreaConfigSource)
   ↑ implemented by
Adapters (FtnMsgBase over the msgbase/ format drivers, AppConfig,
          FidoconfigParser, AreasBbsParser, CharsetDetector, IconvRecoder,
          MsgBaseLastReadStore)

Support (Result) — the bottom. It includes nothing of the project and
                   every layer above may include it.
```

`nodelist/` and `echolist/` stand beside the adapters and lean on the domain, on
`config/` and on `archive/zip_reader` only — nothing in the core knows either is
there. The one thing above the nodelist that does is `ui/nodelist_dialog`; the
echolist is reached only by `main.cpp`, which wraps the area source in it.

`archive/` is the zip reader, and it is what wants zlib. Both subsystems read
archives and neither may reach through the other to get at one, which is why it
is a library of its own (`amberedit_zip`) rather than part of either.

Rules that hold the design together:

- `domain/` includes no terminal or msgbase headers. It is plain structs.
- **ncurses exists only inside `ui/term/`**, and only in `color.cpp` and
  `terminal.cpp` — both through `ui/term/ncurses.hpp`, the one place that decides
  which header to include and takes the C macros (`lines`, `columns`, `clear`, …)
  back off. Nothing above `ui/term/` names a curses type, which is what lets the
  tests link the drawing and measuring code without a terminal.
- The `I*` ports do not change without reviewing every adapter that implements
  them and every consumer in `app/` and `ui/`.
- The format drivers (`SquishBase`, `JamBase`, `SdmBase`) speak bytes and never
  escape `FtnMsgBase`: no file descriptor, raw offset or stored charset appears
  above the adapter layer. Every write and delete goes through `FileLock`, which
  takes every file of the base and releases them as one.
- Character-set conversion happens at the adapter boundary. Above `IMsgBase`,
  every string is UTF-8 and single-byte encodings do not exist.

## Code conventions

- C++17. Formatting follows `.clang-format` (Google style, 90 columns), linting
  `.clang-tidy`. **The project is clean under its own lint config — keep it that
  way.** `tests/.clang-tidy` inherits the root config and drops the two checks
  that only fire on doctest's macros. Every disabled check carries a comment
  saying why; adding one means adding the reason with it.
- Const member functions that only compute a value are `[[nodiscard]]`.
- **Everything in the repository is written in English** — comments, UI strings,
  exception messages, test names, commit messages. The only Cyrillic in the tree
  is deliberate test data (charset conversion, UTF-8 column alignment, a token
  that must not parse as an FTN address); do not "translate" those literals.
- Comments explain why, not what, and describe the code as it stands — no "this
  used to be", no note about what a function was called before. See the rule at
  the head of this file: it covers the comments as much as the documents.
- **A message's `Snt`, `Loc`, `Pvt`, `K/s` and the rest are its *attributes*,
  everywhere** — `domain::attr`, `MessageHeader::attributes`,
  `messageAttributes()`, `ui/attributes_dialog.cpp`, the `Attrs...` button. Never
  "flags": FTS-0001 calls them attributes. "Flag" is left for a boolean on a
  struct, a command-line option, or a bit in someone else's format
  (`NodeEntry::flags` are FTS-5001's node flags and stay that).
- **Everything that can fail answers with `Result<T>`** — `tl::expected<T,
  std::string>` from `support/result.hpp` — and the error is the sentence a
  person reads, already complete. Nothing throws, nothing keeps a `lastError()`
  to be asked afterwards, and no bool means "look somewhere else for why".
  `std::optional` still means *absence* and is not a failure, `std::error_code`
  with the non-throwing `std::filesystem` overloads is still how the filesystem
  is asked, and a function answering a plain question — `isOpen()`, `count()` —
  is still a bool. A Result is read through `*` after being checked, never
  through `value()`.
- Errors a user can act on **before** the screen opens — a config, a theme, a
  template, a keyboard layout — come back out to `main()`, which names the file
  and prints them. Once the interface is up there is nowhere to say anything:
  there is no status line, and a screen that cannot do what was asked simply
  does not do it, having said so by drawing its menu button dimmed. The one
  exception is `ui/error_dialog.cpp`, for an area that will not open — asked for
  by a key that had every reason to work. A second use for it wants an argument
  first.
- **`main()` and the UI's per-frame and per-keystroke handlers still catch**, and
  those three are the only places that do. Not for AmberEdit's own errors, which
  are values, but for what the standard library throws underneath: `bad_alloc`,
  `std::stoll`, the `std::filesystem` overloads that take no `error_code`. A
  broken area must never take the application down.
- **A diagnostic per item is a field and not a `Result`.** A list where one
  member is broken and the rest are fine — `AreaEntry::error`,
  `CompileReport::problems`, `CopyCommand::error`, `StartingText::error` — is a
  report to be shown beside the things that worked, and a Result there would
  throw the answer away to keep the complaint.
- There is no toolbar and no status line, and no bottom bar goes back in: the
  rows one would take are the message's. What a screen offers is in the menu
  behind its top-right corner.

## Domain notes

### Messages, indexes and read marks

- **Message indexing is 1-based** throughout `IMsgBase` and the format drivers.
- **A lastread mark is a UID, never a position.** All three formats store the
  identifier that outlives a pack — the UMSGID in Squish, the absolute message
  number in JAM and Fido `*.msg`. `IMsgBase::uidOf()` / `indexOfUid()` are the
  only conversion and need the base open, which is why `AreaManager` does it
  rather than the store. Writing a position would point every other reader at the
  wrong message the first time the base is packed. `indexOfUid()` asks the driver
  for the nearest earlier message, so a mark on a deleted one lands on the
  message before it — the answer GoldED settles on.
- **The three lastread files have code of their own**, apart from the format
  drivers: `msgbase/lastread_file.*` does the byte-level I/O, one store per
  format sits on top, and `MsgBaseLastReadStore` picks between them by base type.
  Marks go to disk on every message opened, so a reader killed mid-area resumes
  where it was.
- **JAM keys its records by a name CRC, not by a user number.** `lastread_user`
  only fills the record's `UserID`, and without the config's `name` there is no
  key at all, so that format keeps no marks. The CRC is the reflected `edb88320H`
  polynomial seeded with `ffffffffH` and *not* inverted at the end, over the name
  with `A-Z` lowered and nothing else — inverting it, or lowering Cyrillic too,
  gives a hash no other reader agrees with.
- **"Read" means three different things here, and they are three fields.**
  `MessageHeader::isRead()` is FTS-0001's `MSGREAD` — the *network* saying a
  netmail reached the node it was addressed to. The area list's unread count is a
  *position*: how many messages stand after the lastread mark, moved by
  `AreaManager::markRead()`. `MessageHeader::seen` is the mark on the one
  message, which is what the list paints under `highlight_unread`. Reading a
  message out of order marks it and leaves the count where it was; the two are
  meant to disagree.
- **`seen` is a field of its own and not another attribute bit.** Only Squish has
  a bit for it (`MSGSEEN`, `0x00080000`) and `SquishBase` takes it *out* of
  `RawHeader::attributes` on the way in and puts it back on the way out, so it
  never reaches `domain::attr`. JAM and Fido `*.msg` count reads instead
  (`TimesRead`, the `times_read` word at offset 164), and any count is the mark.
  Keeping it out of the attributes word is what stops the attributes dialog
  offering to clear it and "zap all attribs" clearing it. All three drivers'
  `replace()` carry it over from the stored message, beside the arrival stamp and
  the thread links.
- **`markSeen()` patches the field where it lies** — four bytes in Squish, four
  in JAM, two in a `*.msg` — taking the lock like every other write and re-reading
  under it. `replace()` would rewrite the whole record and re-date the message.
  A message already marked is left alone and true comes back, so reading back
  over an area writes nothing; JAM answers that from its in-core header table. It
  is called from `loadMessage()` whatever `highlight_unread` says, and a failure
  is deliberately silent: a read-only area is the ordinary reason.
- **`Uns` is a virtual attribute, derived and never stored.**
  `domain::messageAttributes()` turns the XMSG bits into the short forms readers
  show, and `Uns` stands for no bit: it shows exactly when `Loc` is set and `Snt`
  is not — `MSGLOCAL` with `MSGSENT` clear. Nothing writes it, nothing reads it back, and the attributes dialog has
  no checkbox for it, so the editor shows it over a message being written exactly
  as the reader shows it over one read back (`[Uns Loc]` on a new message). The
  bit values are spelled out in `domain/message.cpp` rather than included from
  `msgbase/`; they are FTS-0001's and are the currency every driver translates
  into, JAM keeping bits of its own on disk.
- **Dates** in XMSG are DOS-packed, the year counting from 1980 and seconds
  stored in two-second units.

### Kludges and service lines

- **Kludges** are control lines starting with `\x01` (^A). In Squish and JAM they
  live apart from the text (a control block, subfields), in Fido `*.msg` inline
  at its head; the drivers hand them all back as `RawMessage::control`, and
  `splitBody` in `ftn_msgbase.cpp` marks them. `PATH:` carries a ^A; `SEEN-BY:`
  does not but is service data all the same. So is the `AREA:` line a packet
  carries — no ^A, and service data **only as the very first line**, where
  FTS-0001 puts it and where `splitBody` looks; the same characters further down
  are a line somebody wrote. Since ^A cannot be printed, `@` stands in for it, so
  `@PATH:` is right and `@SEEN-BY:` and `@AREA:` are not. The reader hides
  service lines and shows them on `k`, in dark grey and **in the position the
  base stores them** — AREA:/MSGID ahead of the text, SEEN-BY and PATH after the
  origin. Do not gather them into a block. `preservedLines()` puts the ^A-less
  leading one back at the head of a message being changed, and `standsFirst()`
  keeps a MSGID from being inserted in front of it.
- **What the reader is showing is what an answer carries.** `BuildRequest::kludgesShown`
  is the reader's `k`, and `quotableLines()` keeps the control lines with the text
  when it is set: a reply quotes them under the same initials as everything else,
  a forward passes them on where `@message` stands. They go in as the reader shows
  them, `@` for ^A, since what is carried is text *about* a message and not
  control data of the answer's own — a forwarded `SEEN-BY:` is one this reader
  hides again on `k`, and that is right: it is service data wherever it stands.
  The tearline and origin are left out whether the kludges are on or off; the
  message being written closes with a pair of its own.
- **Tearline and origin.** `domain::markTrailer()` flags the pair closing a
  message, walking back from the last line and stopping at the first thing that
  is neither; kludges and blanks are stepped over, since SEEN-BY and PATH sit
  after the origin. It has to be decided over the whole body, because `---` is
  also used mid-message as a separator and only the closing one is a tearline.
  The flag travels on `MessageLine`, set by the adapter, so the reader only
  renders it. `isOriginLine()` checks the prefix and nothing else: the
  parentheses may hold a 4D address, a 5D one, or the network name too.

### Keys, modifiers and the mouse

- **A modifier is a flag on the event, not a byte sequence.** `Event::ctrl()`,
  `alt()` and `shift()` are settled in `ui/term/terminal.cpp`, which teaches
  ncurses the sequences terminfo does not describe — kitty's `CSI u`, xterm's
  modifier 3 and 9, `ESC`+letter — through `define_key()`, so a binding is
  written once (`isCtrl`, `isAlt` in `ui/event_util.hpp`). The bare
  `ESC`+letter form of **Alt+letter** is ambiguous with Escape then the letter,
  so only the letters a layout actually binds claim it: `KeyMap::altLetters()`
  hands them to `Terminal`, which passes them to `registerModifiedKeys()`. All 26
  `CSI u` forms are registered already.
- Escape needs no repair: with the kitty protocol on it arrives as `CSI 27 u`,
  without it ncurses resolves the ambiguity on its own timer
  (`set_escdelay(25)`).
- Shift+Space needs the terminal to report modified keys, which none does by
  default. `runApp()` turns that on (kitty keyboard protocol at the disambiguate
  level, plus xterm `modifyOtherKeys=1`) and restores it on exit. The side effect
  is that a bare Escape then arrives as `CSI 27 u`, which `app_shell.cpp` folds
  back into `Event::Escape` before the screens see it — if you raise the kitty
  flags, Enter, Tab and Backspace will need the same treatment.
- `q` and `e` are the reader's for replying and composing, so quitting is
  `Ctrl-Q` by default — `app.quit`, which a layout may move like any other
  command. Two things make Ctrl-Q work, both in `app_shell.cpp`: flow control has
  to be turned off, or the line discipline eats Ctrl-Q as XON, and the chord has
  to be matched in two forms — the raw C0 byte and the kitty protocol's
  `CSI <codepoint>;5u`. The input layer folds both into one event. `Ctrl-C` is
  bound to nothing and `raw()` keeps the terminal from making a signal of it, so
  it does nothing at all until a layout gives it something to do.
- The wheel moves a line per notch on every screen — the cursor in the lists, the
  body in the reader — through `ui::wheelDelta()` (`ui/event_util.hpp`), which
  returns -1/0/+1 and leaves each screen to decide what that moves. A list whose
  rows stand more than one line tall counts those notches rather than taking one
  for a row; the bullet under this one is how. The mouse is
  turned on in `Terminal`'s constructor with `mouseinterval(0)`: without it
  ncurses holds a press for a fifth of a second to see whether it becomes a
  double click. Acting on the press alone matters too — a terminal that also
  reports the release would move two lines per notch. Which protocol is used is
  terminfo's decision: an entry with `xm` gets SGR 1006 and works at any width,
  one without falls back to the original mode, whose coordinates stop at column
  223. Apple's terminfo has no `xm`; every current Linux one does.
- **A wheel notch is a line, and a row of a list may be several.** Where
  `arealist_format` or `msglist_format` holds a `\n`, the two list screens hand
  each notch to `AppState::wheelSteps()` (`ui/app_state.hpp`) with the height of
  their row, and it answers with the notch or with 0: the first notch of a run
  moves the cursor and the rest of that row's worth are swallowed, so a two-line
  row costs two notches. `ui::WheelThrottle` (`ui/wheel_throttle.hpp`) is the
  whole of the arithmetic and holds no clock — `AppState::monotonicMs` is read
  for it, which is what lets a test flick a wheel. A run is notches one way
  arriving no further apart than `list_wheel_throttle_ms`; slower than that, or
  with `list_wheel_throttle off`, every notch moves a row. **A swallowed notch is
  still handled** — the screen returns true and does nothing — or the event would
  fall through to whatever is under the list.
- Horizontal scrolling is deliberately unhandled, and "swipe to the next message"
  is not to be reached for again without a new signal: nothing the terminal
  reports carries an event's phase, so a trackpad's momentum tail cannot be told
  from a fresh swipe, and any single threshold either locks out a quick second
  swipe or lets one flick step through ten messages.
- Measuring wheel behaviour needs a pty harness that drains the app's output
  continuously. A reader that sleeps between reads lets the terminal buffer fill,
  which blocks the app mid-frame and shows up as phantom extra events.
- **A click is shown before it is acted on**, for `click_animation_ms`. A button
  is taken to the theme's `animated_button_text` — `dialog_flash` where the
  button stands inside a box — frame and all; a list moves its
  cursor onto the row the pointer landed on and holds the frame before opening
  it. Only clicks get it: the keyboard has shown what Enter would act on long
  before the key is pressed. `AppState::showClick()` is the whole of it — it sets
  `pressed`, calls `holdFrame` and clears it. `holdFrame` is set in `runApp()`,
  where the terminal is; the tests leave it unset and every click acts at once.
  It draws a frame, so **anything a render pass rebuilds is gone across the
  call** — `readThreadLinks` and the menu's buttons are rebuilt every frame, so
  what the click needs is copied out before it is shown, not read after.

### The area list

- `Esc` opens the quit confirmation in `ui/quit_dialog.cpp`. It is not a
  Navigator screen — it draws over whatever was rendered and, while it is up,
  `app_shell.cpp` feeds every key to it and to nothing else. Ctrl-Q is matched
  before that check so the unconditional quit stays unconditional.
- **`/` goes to the next unread area** — downwards from the cursor and round the
  end, skipping areas that cannot be read: the test is the star column's, since
  an area with no readable base has no unread messages to go to. The slash is a
  command and not a character to search by, so `searchInput()` refuses it and
  `handleEvent()` claims it; the pair holds for the space as well, and both keys
  are free because no area tag holds either character.
- **Ctrl-R reads everything again** — the tosser config and every base, which is
  what brings the counts up to date after a tosser run. Deliberately two steps:
  the key only sets `AppState::rescanning`, and `runApp()` does the reading on
  the pass after the frame carrying the "Rescanning areas..." modal, because
  opening every base blocks and there is no second thread to draw from. Hence
  also `AppState::drawFrame` beside `holdFrame` — the same frame without the
  pause — for drawing from inside a call the loop is stuck in: `reload()` takes a
  callback naming each area as it reaches it, and the modal's second line is that
  name. Keys pressed meanwhile are thrown away (`Terminal::flushInput()`): they
  were aimed at counts being rebuilt. The cursor is put back by tag and path, not
  by position, since the list is sorted again and the config may name different
  areas. `reload()` builds the new list beside the old one and swaps at the end,
  the modal covering a box in the middle of the list and no more.
- **The counts are read again, never adjusted.** `AreaManager::refreshArea()` is
  what a message written, changed or deleted calls, and it re-reads both counts
  off the base and the mark on disk — the open base where the area is the open
  one, a handle opened for the reading otherwise. Adding one to
  `AreaEntry::total` would be right only while nothing else writes to the base,
  which is exactly what a tosser does between two keystrokes. The moved reply
  refreshes the target while its base is still open.
- **Entering an area lands in the reader, not the list.**
  `AreaManager::startingMessage()` decides: the message after the lastread mark
  when it points at one that still exists, and **the first message where nothing
  has been read** — no mark on disk, or one older than anything the base holds.
  An area is read forwards, and the front is also where ← off the front has to
  come back to. Landing after the mark rather than on it is
  `reader_lastread_auto_next`. The list is pushed on top of the reader by `l` and
  popped by Enter or Esc, so it never stacks a second reader. An empty area opens
  in the reader too, on blank rows, that being the screen a first message is
  written from; `l` there refuses with "the area is empty". `leaveArea()` handles
  every exit, whatever depth it is taken from.
- **An area with no base on disk has one made on the way in.** A tosser config
  declares areas before anything has written into them, so refusing such an area
  would refuse exactly the one a first message is wanted in.
  `AreaManager::openArea()` is the only place it happens — `reload()` opens every
  base there is, and creating them all at a rescan would write a spool nobody
  asked for. It acts on `FtnMsgBase::isAbsent()`: the area states a type and
  **nothing at all** stands at its path. A base that is half there, or there and
  unreadable, holds something an empty one written over it would take with it; an
  area whose type nothing states has no format to guess at.
  `FormatDriver::create()` is the format's half.
- **An area that will not open says so and stays on the list.** The row is drawn
  dimmed, but Enter on it is tried like any other — the dimming is what was true
  at startup, and the base may have been written since. Only once opening *and*
  creating have both failed is there anything to say: `AppState::errorMessage`,
  drawn by `ui/error_dialog.cpp`, acknowledged by Enter, Esc, Space, `o` or its
  one button, and acknowledging puts the user back on the area list.
  Entering an area that *did* open calls `AreaManager::refreshArea()` where the
  row had said it could not be read.
- **The order is `arealist_sort`'s.** `config/app_config.cpp` parses the letters
  and `app::sortAreas()` applies them once, at the end of `AreaManager::reload()`:
  sorting by unread needs counts that only exist after every base has been
  opened, and re-sorting as messages are read would move the list under the
  cursor. `std::stable_sort` is the whole of what "areas the criteria cannot tell
  apart keep the tosser config's order" means.
- **What a row holds is `arealist_format`'s.** The shape of the value —
  letters, the width after each, a space standing for itself, `\n` for the next
  line of the row, at most sixteen lines and no empty ones — is
  `config/list_format.*`'s, shared with `msglist_format` so that a rule which
  held for one list cannot fail to hold for the other; which column a letter
  names is all either setting decides for itself. (The config file knows no
  escapes of its own, so the backslash of a `\n` arrives as text.) A blank line
  in a row is written as a space; an empty one is a `\n` typed once too often
  and is refused. `config/app_config.cpp` maps the letters onto an
  `AreaListFormat` — a line per line of the row, and on each the `AreaListField`s
  written on it — and `ui/area_list_format.*` lays them out a line at a time:
  fields with a width keep it, fields written `0` share what is left of *their
  own line* equally and the first takes the odd column. The line names one format
  or two — `"e c u\nd n" "e d c un"` by
  default — and `AppState::areaListFormat()` picks between them on every frame by
  `adaptive_ui_threshold`, the same line `when_narrow` and `when_wide` are read
  against; one format stands for both windows. Each format is one config value,
  so a third value is refused rather than joined: a format with a gap in it is
  quoted. The single heading row is built from the format's first line, so the
  screen knows nothing about which columns there are. A window too narrow for the
  fixed widths gives the flexible fields nothing and the row is cut at the right
  edge; counts too wide for their column are shortened rather than cut (`17k`,
  `1M`, `+`), a column that overflowed moving every field beside it.
- **A row may be taller than a line, and the list counts areas.**
  `AppState::areaRowHeight()` is how many lines the format this window follows
  asks for and `areaListItems()` how many whole areas that leaves room for in
  `areaListRows()` lines; scrolling, paging, clamping and the scrollbar's thumb
  are all counted in areas, and the lines at the bottom no whole row fitted in
  are left blank. Any line of a row is a click on that area. The two formats need
  not be the same height, so dragging the window across `adaptive_ui_threshold`
  changes how many areas a screen holds: `area_list_screen.cpp`'s
  `reflowOffset()` runs at the top of `render()` and holds the line the selected
  row starts on, working the areas above the cursor out again for the new height
  — `AppState::areaRowHeightShown` is the height the offset was last settled
  against. `clampCursor()` still has the last word near the end of a list.
- **A row's description may come from two places.** The tosser config's `-d` (or
  an `area … endarea` block) and the compiled echolist both answer for one, and
  `arealist_description_priority` says which wins — see
  [The echolist](#the-echolist). Nothing in the area list knows about it: the
  answer is settled in `AreaConfig::description` before the list is ever built.
  **What the column shows where neither answered is `arealist_description_default`**,
  `no description` by default and blank where the setting is written `""`. It stands
  in at the column and nowhere else — `@CDESC` and `@ODESC` are still empty for
  an area nothing describes, a dash in a message being a dash rather than a
  missing description. **The description column is drawn in `dimmed` whatever it
  holds** — the area's own words as much as the stand-in: it is the one column
  that is prose rather than a fact about the area, and the names and the counts
  beside it are what the eye goes down the list for. That is why a row is drawn
  in `area_format::runs()` — the pieces of it that take one color each, cut only
  where the color changes — rather than as the one string `row()` still joins
  them back into. **The row under the cursor keeps the selection's colors
  throughout**: it is marked out already, and the quiet color on the selection's
  background would be the one thing on the row that could not be read.
- **Both lists draw the reader's scrollbar, and it costs their rows a column.**
  `arealist_scrollbar`, off by default, and `msglist_scrollbar`, on by default,
  one for each screen; `scrollbar::bar()` in the rightmost column, so a list
  scrolls the way the message in it does. It is drawn only where the list is longer than the
  screen — a list that fits has nothing to point at, exactly as the reader shows
  no bar for a message that fits — and where it is drawn the rows are laid out in
  `state.width` less that column, so the text runs up to the bar rather than
  under it: in the area list that narrower width is what `area_format::layout()`
  divides, in the message list what `msg_format::layout()` does. Where a row
  stands more than one line tall the bar is built a cell at a time from
  `scrollbar::thumbOf()` over the *rows* on the screen, each cell drawn down the
  lines its row takes: what the thumb points at is where in the list the screen
  is, not how many lines that came to. The bar stands
  beside the rows alone; the heading, the rule and the message list's title span
  the whole width, as the reader's rule does over its viewport. Neither setting
  has a key to toggle it: `b` is the reader's, about a message running past the
  window rather than about a table with a row count of its own.

### The message list

- **It comes up centred on the current message**, through
  `message_list::centerCursor()`, called at the two moments the list is arrived
  at — `enterArea()` and `l` in the reader — and nowhere else: moving inside the
  list scrolls a row at a time, and a table that re-centred on every keystroke
  would slide under the cursor. Centring is what the lastread mark asks for: the
  mark is usually partway down the area with the unread messages after it, and
  plain "keep the cursor on screen" arithmetic lands it on the bottom row with
  exactly those messages below the screen. Because the offset is decided when the
  list opens, the reader moves the cursor alone as it walks between messages.
- **What a row holds is `msglist_format`'s**, read the way `arealist_format` is
  and laid out by `ui/msg_list_format.*` — `a` number, `f` from, `t` to,
  `s` subject, `d` date, `\n` for the next line of the row. `"a f0 t0 d15\ns"
  "a f t s d"` by default: the narrow row puts the subject on a line of its own
  under the names, the wide one has them all on the one line. The narrow row
  writes the stamp's width out where the wide one leaves it measured — fifteen
  is what `reader_datetime_format`'s default comes to down to the minute, and a
  pinned column keeps the names beside it the same width from one screenful to
  the next. `AppState::messageListFormat()` picks
  between the two on every frame, `msgRowHeight()` is how many lines a row
  stands and `messageListItems()` how many messages the screen holds — scrolling,
  paging, clamping, `centerCursor()`, `ensureHeaders()`'s window and the
  scrollbar's thumb are all counted in messages, and any line of a row is a click
  on that message. `reflowOffset()` at the top of `render()` holds the selected
  row's screen line when the two formats differ in height, exactly as the area
  list's does.
- **The width falls out in three passes, not two**, which is the one way
  `msg_format::layoutLine()` differs from the area list's. First the fields with a
  width of their own, and the number column, which is as wide as the highest
  number that can go in it and never under three (`kAutoWidth`, `digitWidth()`,
  `kMinNumberWidth`) — a handful of messages should read as a column rather than
  as a stray digit against the left edge. Then the Date column,
  which takes what the stamps come to and no more: a stamp too wide is cut at the
  spaces from the end — `fitDate()`: `15 Aug 26 20:28 +0200` → `15 Aug 26 20:28`
  → `15 Aug 26` → `15 Aug` → `15`. A stamp cut mid-word reads as a different
  date; one short of its zone still reads as the date it is, and the order of the
  parts is `reader_datetime_format`'s. The stamps are measured cut to what is
  free and over the visible rows only, so column and stamps do not chase each
  other from frame to frame; the heading is the column's floor where there is
  room for one. Only then do the fields written `0` share what is left. That
  middle pass is the whole point of the ordering: what the stamps do not use is
  worth more to the subject than five empty columns of Date. `render()` gathers
  the visible rows once, into `msg_format::Row`s, so the pass that measures the
  stamps and the lines that draw them cannot disagree. What is laid out is
  `state.width` less the scrollbar's column wherever the bar is drawn — the
  `msglist_scrollbar` half of the bullet under the area list above.
- **A row is colored by five rules, two over the whole row and three over a
  cell.** Row-wide: the current row, and a message nobody has read yet
  (`msglist_unread`, number and date included). Per-cell: a message written here
  that has not gone out (`unsent`, over the whole row it is on), a From or To
  naming the user (`own_name`), and **the subject, which is drawn `dimmed`
  wherever it stands** — the one column that is prose rather than a fact about
  the message, quiet for the same reason the area list's description is. The
  cells come out of `msg_format::runs()` as `Ink`s, cut only where the color
  changes. A row-wide rule leaves its cells plain and paints over the hbox,
  which works because `Painted` draws the child *after* filling the box — an
  inner color always wins, so the two kinds compose rather than compete. The
  selection is the exception, suppressing the cells. Unread sits behind `unsent`
  deliberately: a message that has not gone out has not been read either, and
  painting such a row unread would leave nothing saying it is still sitting
  there. `highlight_unread` turns the unread rule off and nothing else.
- **Which columns a narrow window goes without is `msglist_format`'s to say**,
  and no longer the screen's: the two formats are the setting, and
  `adaptive_ui_threshold` is the line between them. Only the table is concerned
  either way — in the reader's block the To row *is* the message, at any width.

### The reader

- **Only the arrow keys move between messages.** `PgUp`, `PgDn`, `Space` and
  `Shift+Space` stay inside the current message. Walking off the end of the
  *area* leaves it: → on the last message and ← on the first go back through the
  same `leaveArea()` Esc calls, which `reader_edge_exit` turns off. The check
  lives in `switchMessage()`, the one place that has already worked out there is
  no neighbour, and it covers an empty area as well.
- **Walking off the front takes the lastread mark off the area**, through
  `AreaManager::markUnread()`: ← on the first message asks for the message before
  it, which puts the reader before the area rather than on anything in it. Esc on
  that same message leaves the mark where reading put it — the whole difference
  between the two ways out of the first message. An empty area has no first
  message to stand before. `reader_edge_exit` is read straight off `state.config`
  rather than copied into `AppState`: the copies are for what the screens read
  while drawing, and this is read once per keystroke.
- Leaving that way sets `AppState::discardTypeahead`, and the shell answers it
  with `Terminal::flushInput()` after the keystroke: → held down to walk through
  an area opens, on the area list, the area under the cursor — which reopens on
  the message just left, at its end, ready to be walked off again. It is a
  general hook, but nothing else asks for it yet.
- **The scrollbar is drawn only when the body overflows**, so `relayout()` lays
  out twice: at the full width, and again a column narrower if that overflowed.
  The two are entangled — the bar costs the columns that decide whether it is
  needed — and this order terminates, because re-wrapping narrower can only make
  the body longer. `state.scrollbarShown` carries the verdict to `render()`, and
  `b` forces a re-layout.
- **A reply follows the message's own `AREA:` line**, which is `areareplydirect`.
  `app::areaTagOf()` reads the **first line and no other** — anywhere else those
  five characters are a line of the message, and following one would answer a
  reply with whatever somebody quoted. Where the tag names an area the tosser
  config declares and not the one on screen, `compose::startReply()` writes the
  answer into that area, and what follows is the reply into another area below.
  - The three paths are `startReply()`, which decides; the file-local
    `replyHere()`; and the file-local `replyInto()`, which `startReplyTo()` is
    the dialog's way to. `startReplyTo()` calls `replyHere()` when the target is
    the area being read — an area picked by hand is not overruled by a line in a
    message — which is also what keeps the two from calling each other in a
    circle.
  - The setting is read off `AppState::areaConfig`, the area being *read*, not
    off `composeConfig()`: there is no compose area yet when the question is
    asked.
  - **It is a move only as far as the screen is concerned**, which is
    `ComposeFields::direct` beside `::moved`: `buildRequest()` leaves
    `originalArea` null, so no `@moved` lines, `@oecho` is the area the answer
    goes into, and the title says "reply" rather than "moved reply". What `n`
    does is unchanged — that *is* a move.
  - The reader's title names the echo — `localnet from test.other (2:382/736)
    44/44` — whenever the line names another area, whether or not
    `areareplydirect` is on: it is a fact about the message.

- **`reply_to_area` moves the picker's cursor**, and outside this screen says
  where an echo's carbon copies go. `askArea()` looks the tag up in the
  manager's list — case-folded, as every echoid is — and only for `For::Reply`:
  the other three purposes carry the message itself somewhere, which no one
  setting can name an area for. A tag naming no area leaves the cursor at the
  top, since the areas come from the tosser config and a setting cannot add to
  them. It is read off `AppState::areaConfig` and not off `config`, being a
  setting an area group may state.

### Finding a message

`f` in the reader, `find` in `reader_menu`. Three pieces: `ui/find_dialog.*`
asks, `message_read::findMessage()` walks the area, and `encoding::TextSearch`
decides what an occurrence is.

- **The matching is folded by the charset the *message* declares**, never by the
  locale. `<cctype>` is wrong here twice over — under a single-byte locale it
  folds the whole high half of the byte range and would make two differently
  spelled Cyrillic words compare equal, and under a UTF-8 one it folds nothing
  above ASCII at all — so `encoding/text_search.cpp` writes the folding out over
  code points: ASCII, the Latin-1 supplement and Cyrillic, which every charset a
  message states is a subset of. That makes folding the decoded text the same
  answer folding the stored bytes would give, and it is also the right answer for
  a message stating UTF-8.
- **CP866 alone carries the Russian quirks.** A message written in it may spell
  н, р and у with the Latin h, p and y: the glyphs are the same on a DOS screen
  and the keys are one layout apart. The three pairs are folded together — and
  the capitals with them, since the lower-casing runs first — and only under that
  charset, since in a western area they are six different letters. That is the
  one thing the charset is asked, which is why `MessageHeader::charset` exists
  beside `MessageBody::charset`: a header-only search must not read every body to
  learn it.
- **A search starts on the message in front of the user and stops at the end of
  the area.** Nothing wraps round to the front. **The same words looked for again
  start on the message after the one found**, which is what makes `f`, Enter, `f`,
  Enter walk from occurrence to occurrence; `AppState::LastFind` is the whole of
  that memory, and it holds the area as well as the query, because the same words
  looked for in the *next* area are a first search there and starting them one
  message in would pass over its first message.
- **What a search reads is `app/message_search.*`**: `searchableHeader()` is the
  From name and its address, the To name and its address, and the subject — the
  fields the reader draws, in the order it draws them — and `matchesBody()` is the
  lines it shows, service data left out. The header is in both scopes: a search
  of the text alone would answer "no" for the message whose subject is the very
  words typed.
- **Twits are stepped over exactly where the reader steps over them**:
  `twitSkipped()`, so `ignore` passes every twit and `skip` the ones not addressed
  to the user. `blank` and `kill` are not navigation — a twit they hide is found
  like any other message and opened behind `kTwitNotice`, and Space then shows the
  text with the occurrences lit in it.
- **The highlight belongs to the one message the search landed on.**
  `AppState::findHighlight` holds the query and `loadMessage()` clears it, so every
  way to another message takes it off; `findMessage()` puts it back after the
  message it found is loaded and forces one more `relayout()`. The occurrences
  travel on `DisplayLine::found`, filled in by `wrapBody()` and cut up between the
  wrapped rows by `foundForRows()` — the walk `bbs::runsForRows()` makes over the
  color runs, and for the same reason: a break the window happened to fall inside
  an occurrence must not take the highlight off either half of it. The header
  block is lit in `render()` instead, per frame, folding a handful of characters
  costing less than a field to keep in step.
- **`found` is a fill and not a foreground**, with `background` written on it. A
  foreground would be competing with the quote colors, the links and whatever the
  message's own BBS codes asked for, and it has to be seen at a glance from
  anywhere in a long message. One role rather than a pair — the screen's own
  background is legible on anything bright enough to serve here.

### Twits

- **The whole of what decides one is `AppConfig::isTwit()`**: the `twit` lines
  against the From name or address, against the To ones where `twit_to` is on,
  and `twit_subj` against the subject. A `twit` line is an address exactly when
  it holds a ':' and parses as a `domain::AddressPattern`; everything else is a
  name glob, so a bare `*` is the name it was written as and not "every address
  there is". Both globs go through `text::globMatches()`, which is also what
  `AreaTagPattern` matches a `member` line with and what a `nodelist` or
  `echolist` line holding a `*` is matched against a directory with: one matcher,
  so a `*` means the same thing wherever the config holds one. They are read off
  `AppState::areaConfig`, a group's lines *adding* to the file's.
- `twit_mode` is where the five answers part company, and only two touch anything
  outside the reader:
  - `blank` hides the *text* and nothing else. `relayout()` puts one line in
    `readLines` where the body would go, Space in `handleEvent()` sets
    `AppState::twitRevealed`, and `loadMessage()` clears that flag again: having
    asked for one twit is no reason to be shown the next unasked. The header
    block is drawn as always — whose message is being passed over is what the
    user is entitled to see.
  - `skip` and `ignore` are navigation, through `unskipped()`.
    `switchMessage()` walks in the direction of the key; `openMessage()` — what
    entering an area and picking a row both go through — walks forward from the
    message asked for and then backward, those two naming a *place* rather than
    one message. A run of twits at the end of the area is the end of it, which is
    `reader_edge_exit`'s business; where every message is a twit the one asked
    for is opened blanked. The two differ in `skip` sparing what
    `AppState::addressedToUser()` recognises, and what it spares is *not* blanked
    — `AppState::twitHidden()` asks `twitSkipped()` rather than `isTwit()` under
    these two. `goToMessage()` is deliberately not among them: a thread marker
    names one message and is answered with it, blanked.
  - `kill` deletes, and `message_list::killTwits()` does it **once, as the area is
    opened**, backwards so the numbers still to be looked at do not move under
    the sweep. Everything downstream then sees an area with no twits in it — the
    numbers, the list, the counts and the thread markers agree, where deleting
    one at a time would renumber the area under whatever was reading it. It runs
    after `setCurrentArea()`, the twits being that area's settings, and before
    `messageCount` is read.

### Writing a message

- **The header and the text are one screen.** `ScreenId::Compose` is the whole of
  it. `AppState::composeInHeader` is which half the typing goes into and is the
  only difference between them — the cursor is in a field or in a line, never
  both, and the block is drawn the same way either way. A **new message and a
  forward open in the header**, on the To name; a **reply opens in the text**,
  its header having come off the message it answers, with `composeField` left on
  the subject. Esc asks before dropping the message wherever the cursor is.
- **Tab is a ring round the whole of it, the text standing in it where a field
  would.** Forwards: the four fields, the subject, the attributes button, off the
  last into the text, and out of the text onto the **first** stop — not the one
  it was left from, which would cycle between two and never reach the rest.
  `Shift-Tab` (`Event::TabReverse`) walks it the other way: off the **first**
  stop down into the text, out of the text onto the last. The To address is
  skipped in echomail either way, `fieldAfter()` answering for both. **Enter is
  not the ring**: it walks the fields that are typed into and hands the typing
  down to the text off the subject, going past the button. Nor are the arrows:
  `↓` off the subject goes to the button and then into the text because both are
  drawn below it, and `↑` off the From name stops. `Ctrl-U` goes back onto the
  field the cursor was last in — a chord of its own rather than `Ctrl-I`, which
  is the byte Tab has sent since ASCII and would be the ring on all but a
  terminal reporting modified keys.
- **A click is the way between the two halves as well, and puts the cursor where
  it points.** `clickToCursor()` walks `AppState::composeFieldSpots` (one per
  field, in `compose::Field` order) and then `composeTextRows` (one per drawn
  row). Both are filled in by `render()` and hold a `Box` and an `origin`: a
  field narrower than what is typed into it is drawn scrolled sideways, so the
  byte the leftmost column shows comes back from the one place that works the
  scroll out, `ui::inputField()`. `offsetAtColumn()` turns the column into a
  byte, so a click never lands inside a UTF-8 sequence. What is not drawn answers
  no click — the To address is `Box::Nowhere()` in echomail — and a click past
  the end of a field, or on a blank row, lands at the end of what is there.
  Coming down out of the header is the departure Enter off the last field makes,
  `leaveToAddr()` and `refreshTemplate()` and all; the one thing a click does not
  carry over is that a To address that will not parse holds Enter where it
  stands.
- **`ui/input_field.*` is the one place a field is drawn** — the import dialog's
  path and charset boxes included, or the hit test drifts from what was drawn. It
  holds the UTF-8 stepping a cursor needs (`prevChar`, `charLen`, `charCount`)
  beside the drawing that uses it. The compose screen's `field()` is a wrapper
  that adds the `ComposeSpot`.
- **A long line is broken by the window, never by the editor.**
  `softWrapOffsets()` (`text_layout.cpp`) says where the window breaks a line and
  `layoutRows()` (`ui/edit_layout.cpp`) turns the buffer into the `EditRow`s that
  are drawn. Nothing is inserted into the text: a carriage return the editor
  added would go out in the message. **The cursor is laid out with the text**,
  which is why `layoutRows()` takes the whole `TextBuffer`: standing past the end
  of a line it wants a column of its own, so that line is broken as though it
  carried one character more — without it, a line filling the window to its last
  column keeps the cursor past the right edge and `field()` scrolls the row
  sideways. So `AppState::editScroll` counts **rows of the screen**,
  `composeTextRows` holds one spot per drawn row with the byte its left edge
  shows, and the arrows and page keys move by `moveByRows()`: a line four rows
  tall is four presses tall.
- The one thing the editor does break is a **quote**: `insertText()` holds a line
  carrying a quote prefix to `quote_margin` and wraps it under that same prefix,
  which is the whole of what the setting is for. `quote_margin` says nothing
  about what the user types.
- **The text is expanded before the header is filled in, so it is expanded
  again.** `fillFromTemplate()` runs the moment the message is begun, but a
  template greets by `@pseudo` and closes with an origin carrying
  `fields.fromAddr` — fields the user has yet to type. So leaving the header
  calls `refreshTemplate()`, which expands again under two conditions held on
  `AppState`: the header differs from `composeStartHeader` (nothing changed means
  nothing to rebuild, and rebuilding would throw away where the user had scrolled
  to) and the text still equals `composeStartText` (one character typed makes the
  message the user's, and it is never rewritten after that).
- **The addresses are checked when the message is stored, not when the header is
  left.** `addressesReady()` wants the sender's always — it is what the MSGID is
  made of — and the recipient's in netmail. Missing and malformed are one case
  (`FtnAddress::parse` answers both), and the cursor going **up into the header**,
  onto the field at fault, is what says which it is. It is asked twice:
  `askToSave()` before raising the confirmation, so a message that cannot be
  stored is not asked about, and `saveMessage()` again, being the one door the
  message leaves by. Per-field checking is not wanted: an address is typed
  through states that do not parse. The one exception is the To address, since
  leaving it picks the AKA — `leaveToAddr()` runs on every departure, and only
  the forward one holds the cursor on an address that will not parse.
- **A netmail recipient may be typed as a word the config gives a whole address
  to** — `address_macro`, read by `readAddressMacro()` in `config/app_config.cpp`
  and acted on by `applyAddressMacro()` in `compose_screen.cpp`:
  - It is Enter in the To name row of a netmail and nothing else, standing ahead
    of `askNodelist()` in `headerKey()` and short-circuiting it. Echomail has no
    address field for it to fill in.
  - **The whole field has to be the macro**, trimmed and case-folded
    (`AppConfig::addressMacroFor()`). Matching inside a name would make every
    macro a word nobody could write to — an `af` found in `Olaf`.
  - **It answers the whole row**, both halves and whatever stood in them, the
    reasoning `useNode()` fills both halves in by. `leaveToAddr()` then picks the
    AKA and the cursor rests on the subject.
  - The subject and the attributes are separately optional; an empty field is one
    the line did not state.
  - **The attributes are added, not substituted** — `Loc` and `Pvt` are what a
    netmail of one's own starts with. `domain::messageAttributeBit()` reads back
    exactly what `messageAttributes()` writes, off one table, so the config and
    the interface cannot drift apart. `Uns` is refused by name: it is virtual.
- **A name takes 35 characters and the subject 71, and the editor refuses the
  rest.** `fieldLimit()` in `compose_screen.cpp` is asked where a character is
  inserted, and a keystroke that does not fit is swallowed. That is what XMSG has
  room for — 36 bytes of a name and 72 of a subject, the terminating zero among
  them. JAM is roomier (100 characters per subfield), but a message has to
  survive whichever base it lands in, so the tightest of the three is the limit.
  It is counted in **characters**, the message being encoded on the way to the
  base: a Cyrillic name is two bytes here and one in CP866, and a byte limit
  would refuse half a name that fits. `toFixedField()` still truncates at the
  driver — only what is typed is held to the limit. The boxes are drawn one
  column wider than their limit, for the cursor to stand in.
- **The attributes stand under the addresses and are the button that opens the
  dialog.** `headerRows()` carries them on the Date row, right-hand column,
  `[Uns Pvt Loc]`, exactly where the reader shows the same thing — the same
  `messageAttributes()`, so a message reads the same being written as read. A
  message carrying none says `Attrs...`. `Ctrl-F` opens the dialog from anywhere
  on the screen. Six things worth knowing:
  - **The fields around it are drawn as boxes that take typing**, on the
    `input_field` fill and the width of their column (`headerRows()`'s `cell()`).
    A fill says which block may be changed without spending a column either side
    of every field the way a border would, and the name and address columns stand
    hard against each other for the same reason. Only the fields wear it: the
    Date and Recd rows are shown rather than typed into and keep `header` on no
    fill. The field the typing is in takes `selection`/`selection_text` — the
    same fill the lists give the row Enter would act on, so whatever the typing
    is on wears one color everywhere. Both shipped themes state the role.
  - **The button is a stop in the Tab ring**, `kAttributes`, between the subject
    and the text — the one stop not typed into, which is why `headerKey()` hands
    it Enter and Space and then bows out before anything that edits text.
    `moveTo()` leaves `composeCursor` at zero on it and `valueOf()` is never
    asked for its text.
  - The dialog is a checkbox per attribute, turned over by pointing at one, by
    Space, or by the chord printed beside it. Every toggle lands on
    `compose.attributes` as it is made, so the row under the addresses moves with
    the dialog; `Enter` keeps what was done and `Esc` puts back what the dialog
    opened with, the only copy anyone keeps. `Ctrl-Z` clears the lot.
  - The list and the chords are GoldED's, less the attributes AmberEdit has no
    bit for (Archive/Sent, Zonegate, Hub/Host-Route, Xmail, Erase and Truncate
    File/Sent, Locked, Confirm Rcpt Request, the reserved ones), left out rather
    than shown dead.
  - The bits live on `ComposeFields::attributes`, are seeded by
    `app/compose_prefill.cpp` (`kLocal` always, `kPrivate` in netmail) and reach
    the base on `MessageDraft::attributes`. `FtnMsgBase::write()` stores that word
    as it stands and adds nothing of its own.
  - **Every chord is the dialog's while it is up**, `app_shell.cpp` answering it
    ahead of even `app.quit`: `Ctrl-C` is Crash here, and a chord it binds
    nothing to is swallowed rather than passed down. It is the only thing
    anywhere that can claim a key back from a layout, and it closes on Esc. Hold,
    Immediate, Return Rcpt Request and Transit are `Ctrl-H`, `Ctrl-I`, `Ctrl-M`
    and `Ctrl-J`, the bytes Backspace, Tab, Enter and line feed have sent since
    ASCII: they arrive as chords only on a terminal reporting modified keys,
    which is why every attribute has a checkbox as well.
- **The editor draws the reader's scrollbar, a cell at a time.**
  `ui/scrollbar.hpp` holds the thumb arithmetic and the two glyphs; the reader
  takes the whole column (`scrollbar::bar()`), the compose screen a cell at a
  time (`scrollbar::cell()`), an hbox per row. Not a style choice: every row of
  the editor is a row of the frame and `chrome()` counts them to know how much
  blank to leave under the message. The editor lays out twice as the reader does
  (`textLayout()`), and the soft wrap follows the width it settles on, which is
  why `moveByRows()` and `field()` are given `layout.width` and not
  `state.width`. `reader_scrollbar` and `b` have no say here: in the editor the
  bar is the only thing saying the message runs on past the window.
- **The delete-line button walks the message, and the whole message is laid out
  narrower for it.** `ui/delete_line_button.hpp` holds the glyphs — a box around
  the row the cursor is on with a cross in it — and
  `compose_delete_line_button` says whether it is there, the same four values
  every other window-led setting takes. It stands in the **three rightmost
  columns of every row**, not only of the row it closes round: it moves with the
  cursor, and a width that moved with it would rewrap the message at every
  keystroke, words jumping a screen away from what was being typed. So
  `textLayout()` takes those three columns off before the lines are broken, and
  the scrollbar stands in the last of them rather than asking for one of its
  own — which is why a message that overflows is not laid out a second time when
  the button is there. The box closes a row either way and only over rows of the
  message and rows of the window: over the first row there is the rule closing
  the header block and under the last the blank the message stopped at, and a
  side reaching into either would close round what is not the line it deletes.
  It is drawn while the typing is in the header too — the cursor is in the text
  either way, and the three columns are gone either way. `render()` reflects a
  box per row into `AppState::DeleteLineSpots`, three of them because the button
  is drawn a cell at a time with nothing standing over all three rows; the click
  is `deleteLine()`, the same call `Ctrl-Y` makes.
- **The wheel in the editor scrolls the text and drags the cursor** —
  `wheelScroll()`, a row per notch. The cursor stays where it was written while
  it is on the screen and is carried onto the nearest row still showing when the
  window passes it by. It cannot be left off: `render()` calls `scrollToCursor()`
  on every frame.

### Carbon copies and crossposts

- **The commands are in the text of the message, and they are carried out when
  it is stored.** `CC:` sends a copy to somebody else, `XC:`/`XP:` posts the same
  message in other echoes — GoldED's spelling, so that a habit brought from there
  goes on working. `app/copy_commands.*` is everything the text alone decides:
  finding the lines, reading their tokens, finishing an address written in part,
  building the list the message keeps and rewriting the text round it. Who a name
  belongs to is not there — that is the nodelist's answer, and `nodelist/` stands
  where nothing in the core may reach it, so the looking up is
  `ui/screens/compose_screen.cpp`'s.
- **A command has to begin its line**, without regard to case, and only an
  ordinary line can be one: `scannable()` steps over control lines, the tearline
  and origin closing the message, and anything carrying a quote prefix. What
  somebody quoted is what they wrote, and carrying out their `CC:` would send
  copies this writer never asked for.
- **What comes into a message from elsewhere is disarmed by its prefix** —
  `disarmCopyCommand()` writes `CC: Ivan` as `!CC: Ivan`. It runs where a
  forward carries the message it passes on (`contextFor()` in
  `app/message_builder.cpp`, on `@message`) and where a file is imported as text
  (`app/import_file.cpp`). By the prefix and not by the whole line: a line
  carrying recipients is exactly the one worth disarming.
- **Where the copies go is the area's kind.** `carbonArea()`: netmail answers
  with itself, an echo with the netmail area its `reply_to_area` names, and a
  local area with nothing. An echo with nowhere to put them has no `CC:` commands
  at all — `commandsIn()` drops them, so the lines stay in the message as the
  text they were typed as and nothing is said about them. That is what makes
  `reply_to_area` a group setting: it says where an echo's answers belong, and
  both the reply dialog and the copies follow it.
- **Storing asks once, and the run is a state machine.** `saveMessage()` finds
  the commands, puts `Confirm::ProcessCopies` up — the one confirmation whose two
  answers are Process and Ignore, since the message is stored either way — and
  comes back through `processCopies()` or `ignoreCopies()`. `AppState::CopyRun`
  holds how far the walk has got, because a `CC:` naming somebody several nodes
  answer to (or none) opens the nodelist box, and `useCarbonCopy()` is what takes
  the walk up again. `askToSave()` drops the run, so a message typed into since
  the last attempt is asked about afresh.
- **The message is stored before its copies.** A base refusing the message is a
  message nothing was copied on account of. Each copy is built by `copyDraft()`
  against the settings of the area it goes into — its AKA, its charset, its
  tearline and origin — over the text with the editor's own closing pair taken
  off (`withoutTrailer()`), and each is written a second on from the last so that
  their MSGIDs cannot collide. One base is open at a time, so `writeCopies()`
  swaps as `storeElsewhere()` does and opens the reader's own again at the end.
- **Nothing is dropped on behalf of something that did not happen.** A recipient
  nobody could find, a mask no echo matches, a `@file` that will not open: the
  line stays exactly as it was typed and `reportUnresolved()` says what was not
  done. That box reports about a screen that is still standing, which is what
  `AppState::errorEndsScreen` is for — the error dialog's other use resets the
  navigator because it stands in place of a screen that would not open.
- **`compose_cc_list` and `compose_xc_list` decide what the message keeps**, and
  the list stands where the first command line of its kind stood. `keep`/`raw`
  are the only values that leave the command line itself standing; `hidden` is
  control lines rather than text and reaches the base through
  `BuildRequest::extraKludges`. "Originally in" is written only where the message
  really went somewhere else, and a `#` on the mask covering the area being
  written in leaves it unsaid.

### Writing into another area

- **`n` and `m`** — answering the message on screen there, and passing it on in
  one of three senses — with `reply_to` and `forward` in `reader_menu` for the
  same two. `ui/area_dialog.*` is the modal both open: every area the tosser
  config declares, by name, in the order `arealist_sort` puts the area list in.
  It opens on the first of them and is searched by typing a name the way the area
  list is. `AreaPicker::purpose` is which of the four asked, and `app_shell.cpp`
  starts the right message once an area is picked, copying the `AreaConfig` out
  of the manager's list first, since everything after that opens and closes
  bases.
- **`m` asks what before it asks where.** `ui/forward_dialog.*` opens first —
  Forward, Move, Copy, three buttons on the confirmation's pattern, answered by
  ←→ and Enter, by their initials, or by a click. Forward is a message of one's
  own carrying this one; Move and Copy put *this very message* into the other
  area and differ only in whether it stays here. `AppState::ForwardPicker::Mode`
  becomes an `AreaPicker::For` in `purposeOf()` and nothing acts until an area
  has been picked. It opens on Forward deliberately: it is the one answer that
  writes nothing by itself, and Move empties this area the moment an area is
  named.
- **A move and a copy are not the compose screen at all.** `app::copyOf()` builds
  the draft out of the message as the base holds it — header fields, attributes,
  the kludges either side of the text, `preservedLines()` making the same split a
  change makes — and `message_read::copyMessage()`/`moveMessage()` hand it to the
  other base through the same swap a moved reply makes. Two things follow from
  "the same message" rather than "a message like it": its **MSGID travels with
  it**, that being what the network tells two messages apart by, and it **keeps
  the date it was written on** (`MessageDraft::written`, the one field a base
  reads off the clock unless the draft states it). The arrival stamp is not
  stated: when the message reached the base it is going into is that base's own
  business. A move deletes **only once the write has come back with a number**,
  and then leaves the reader where a delete leaves it. Picking the area being
  read is answered by each in its own way: a move there does nothing, a copy
  there is a second copy in the open base, no swap involved.
- The two that *are* written — the moved reply and the forward — are the ordinary
  message with three changes. The prefill is against the area picked, not the one
  being read (its AKA, and whether there is a recipient — an echo answered into
  netmail is written as netmail); `ComposeFields::moved` or `::forward` is set
  and `AppState::targetArea` holds where it is going, which is what
  `AppState::composeArea()` answers with from then on; and
  `BuildRequest::originalArea` carries the area left behind, which `@oecho` names
  and which turns the template's `@moved` lines on.
- A forward is a *new* message that happens to carry one: `original` is set as
  for a reply, but `fields.forward` makes the context `@new` rather than
  `@reply`, fills `@message` instead of `@quote` (both are unconditional inserts
  in the template GoldED ships, and filling both would put the message in twice)
  and writes no REPLY kludge. Its subject comes from the message it passes on,
  and its editor opens **where `@position` says**: the bare `@position` answers
  for a forward, since `@quoted@position` stands on a line only a reply reaches
  and the later one wins where a template has both. That is how a reply lands on
  its quote and a forward on the line above the signature.
- Saving hangs on `AppState::composeGoesElsewhere()` rather than on which key
  began it — a forward into the area being read is an ordinary new message.
  **One base is open at a time**, so `storeElsewhere()` swaps: `state.base` is
  dropped, the target opened and written to, and the area being read opened again
  straight after. Nothing on the screen underneath comes off the base while it is
  away — the header and body being read are copies, as are the list's headers —
  so the reader is left exactly as it was, scroll and all, and dropping the
  message needs no swap at all. A target that refuses the message keeps the
  editor open on it; a source area that will not reopen ends on the area list.

### Changing a message

- **`reader.change` in the reader** — `c` and F2 by default, with `change` on
  `reader_menu`. It is the compose
  screen again, with three differences, all hanging on `ComposeFields::changing`.
- The editor opens on the **message itself** rather than a template —
  `compose::startChange()` fills it with the body's visible lines and the header
  block from the message's own fields — so `refreshTemplate()` is held off
  entirely (it would throw the message away) and `addressesReady()` passes
  without asking (nothing is made from those addresses, and JAM keeps no sender
  address in an echo area). What the editor does not show is kept in
  `AppState::changeKept`: `app::preservedLines()` splits the service lines where
  the text begins, and `app::buildChange()` puts both back around the text when
  it is stored, in the charset the message was read in. No REPLY is invented and
  no tearline.
- The exceptions are the two control lines that describe the *writing* rather
  than the message, both from `app::ChangeStamp`: a **new MSGID**, naming this
  system (`app::ownAddress()`, falling back to the message's own From address
  and, with neither, leaving the old line alone — a MSGID without an address is
  not one) and this moment; and **TZUTC**, which says which clock the new date
  stands on. A message carrying neither is given both. In-base threading survives
  — the links are the base's own and `replace()` keeps them — but a REPLY made of
  the old MSGID on another system no longer names it.
- Two of the three ways in ask first, and the question is always about somebody
  else's copy. A message whose sender is not one of ours
  (`AppConfig::isOwnAddress()`, over the header's address or — in a JAM echo,
  which stores none — the one its MSGID names) is one we would be writing in
  another person's name, and answering yes puts the template's `@Changed` lines
  at the head, expanded with the *current user* as `@CName`/`@CAddr` rather than
  with the From fields, which here are the original author's. One of ours
  carrying `MSGSENT` is out of our hands already, and answering yes takes that
  attribute off (`app::change()`, the one thing the prefill does not carry
  across), so the message counts as unsent again. Our own unsent message opens at
  once.
- Saving goes through `IMsgBase::replace()` rather than `write()` and comes back
  to the reader on the same message number. `AreaManager::refreshArea()` is
  called for the same reason a delete calls it: the counts move with the
  attributes.

### Dialogs

Every modal shares four habits, and a new one is expected to keep them: it is
**measured once off the window** by `fitBox()` and keeps its size until the
window changes (a box measured against what it is showing would be a different
size in every directory and every area, and the row under the pointer would move
as it was opened again); where it has a frame it is `ui/dialog_frame.*`; it ends
in **`dialog::surface()`** rather than in `clear_under` — the wipe with the
box's own `dialog_background` and `dialog_text` laid down over it, so that no
cell of a dialog is left in whatever color the terminal draws with when nothing
is asked for; and a click outside it dismisses it without the screen underneath
acting on the click.
`ui/dir_listing.*` and `dialog::bottomBar()` are shared the same way — the bottom
rule carries the keys, and what went wrong in their place, rather than either
taking a row.

- **`i` shows what the base holds about the message** — the storage rather than
  the message: the stored header field by field, the records naming it, and a
  hexdump of the bytes each is made of. `ui/info_dialog.*` shows and does not
  ask: every key either moves inside it (↑↓, PgUp/PgDn, Space, Home/End, `g`/`G`,
  the wheel) or puts it away (Esc, Backspace, Enter, `i`, a click anywhere). The
  report comes from `IMsgBase::info()` and is read **once**, when the box opens —
  a base behind a scrollbar would be read on every frame. The box decides the
  layout: eighty columns at most, which is what sixteen bytes to the row takes,
  and in a narrower window the dump gives up bytes to the row (eight, then four)
  rather than running off the edge, so the rows are laid out again when the width
  changes. The column beside the bytes is **printable ASCII and dots, not the
  message's charset**: what is being looked at is the bytes.
- **`Ctrl-O` in the editor reads a file into the message**, through
  `ui/import_dialog.*`: the path, the directory it names — walked with the arrows
  and searched by typing — and under it the mode, Text or UUE. Tab walks the
  three stops, Enter acts wherever the typing is, Esc closes. It is the only
  dialog anywhere that touches the disk.
  - **The path under the title is a field, not a label**, and Enter on it is
    answered by the filesystem rather than by a mode the user has to set first:
    an existing directory is walked into, an existing file is read, anything else
    is `Path not found`. It takes a leading `~` as a shell does and reads a
    relative path against the directory on screen. Walking the listing puts the
    box back to where the listing now is, and Esc puts the directory back before
    closing the dialog.
  - **A row is a name, a size and a stamp.** The size is
    `area_format::countText()` — the same shortening the area list's counts use —
    and a directory says `<dir>`. The stamp is `reader_datetime_format` through
    `MessageDate::format()` with **no zone passed**: `%z` says which clock a
    *message* states it was written by, and a file on this disk states nothing of
    the kind. Its column is measured off a sample date, not off the files, so the
    columns do not shift as one is walked into. Both are read once, with the
    listing.
  - **The two modes are two different things, and only one is text.**
    `app/import_file.*` holds both. Text is decoded into UTF-8 and fenced by
    `import_begin`/`import_end`. UUE is *encoded* rather than decoded and carries
    its own `begin 644 …`/`end`, so nothing is written round it. A zero goes out
    as a backquote rather than the space the original encoding used — mail strips
    a trailing space and the line would arrive a byte short.
  - **What is read is made safe to carry.** Tabs are opened out to the next
    eight-column stop and every other control byte is dropped. The NUL is the one
    that matters: FTS-0001 ends a message at the first one.
  - **The charset is the locale's**, the one `term::ensureUtf8Locale()` settled
    on. `ImportRequest::charset` is still the caller's to name, since `app/` has
    no business reaching into the terminal's locale.
  - **Where it lands is where the cursor is, as whole lines**: at the cursor when
    it stands at the start of a line, after that line otherwise, the cursor
    coming to rest under the block. Ctrl-O is answered from the header as well. A
    file that will not open is a line inside the dialog, not a modal over it —
    which is also why the *dialog* reads the file: `app_shell.cpp` only takes the
    lines and hands them to `compose::insertImported()`. The directory and the
    mode outlive the dialog, on `AppState` rather than in the picker.
- **`f` in the reader looks for a message in the area**, through
  `ui/find_dialog.*`: the words in a field, and under them a pair of radio
  buttons saying how much of a message to read them against. Tab walks the three
  stops — the field, the answers, the **Find** button — and Enter searches from
  wherever the cursor is, the button being where the ring comes to rest rather
  than the only place the box can be answered from. A radio is turned over by
  pointing at it or by ↑↓ and does nothing else: it says what the search will
  read. A search that came to nothing leaves the box standing with the words
  still in it, saying so in the bottom rule — the export box's habit, since the
  words are there to be changed and a box that vanished would have to be opened
  again. The box asks and does not search: what walks the base and moves the
  reader is `message_read::findMessage()`, which the shell calls with what the
  box holds. See [Finding a message](#finding-a-message).
- **`w` in the reader writes the message out to a text file**, with `export` on
  the list `reader_menu` may name. `ui/export_dialog.*` is the modal and
  `app/export_file.*` does the writing; it is the import box with the answers the
  other way round, sharing its frame and listing. Its own five:
  - **The listing is directories and nothing else** — what is being picked is
    somewhere to write, and there is no size column for the same reason. The name
    is *typed*, in the box under the list.
  - **A file already there is a question, not a policy**: `File exists:`, the
    name, and **Overwrite** or **Append** (`o`/`a`, ←→ and Enter, or a click),
    with Esc for neither, which leaves the name in its box to be typed over.
    Appending is right for collecting messages into a digest and wrong for every
    other reason a name is typed twice, and there is nothing in the message to
    tell the two apart. `app::ExportWrite` carries the answer down; nothing below
    the dialog decides it. The question is held in `ExportPicker::Existing` and
    drawn over the box that raised it. Only text mode raises it.
  - **Nothing invents a name.** The box holds what the last export was called and
    nothing at all until one has been; Enter on an empty one answers `No file
    name`. The name a file *was* written under stays, as does the directory.
  - **What is written is the reader's own header block and then the text** —
    From, To, Subj and Date under the same labels, the rule, and the message with
    its service lines left out exactly as the reader leaves them out. The stamp
    is `reader_datetime_format`, so the file says what the screen said, and the
    rule under each header block is what keeps two appended messages apart.
  - **The charset is the locale's**, as for an import; `ExportRequest::charset`
    is still the caller's to name.
- **A message carrying uuencoded files is asked about before it is written.**
  `app::uueFiles()` reads the message when `w` is pressed (`askExport()` is the
  whole of the decision), and where it finds anything `ui/export_mode_dialog.*`
  goes up in the export dialog's place: **Files**, taken back out of the message,
  or **Text**, the message as it is read. The export dialog follows either
  answer, so the two boxes are one question in two halves exactly as `m`'s two
  are. It is the import's UUE mode run backwards, `app/export_file.*` holding it
  beside the text export the way `app/import_file.*` holds `uuencode()`.
  - **A block is `begin <mode> <name>`, data lines and `end`, and a block with no
    `end` is not a file.** That is also how a message carrying **one section of a
    file split across several** is passed over — multi-section UUE is not
    supported, and half a file decoded into a whole one will not open. A block
    damaged in the middle is dropped rather than decoded as far as the damage;
    scanning goes on from the line after its `begin`, so a good block under a bad
    one is still found. Several files in **one** message are ordinary.
  - **The length character at the head of a data line says how many bytes it
    carries**, and the line is padded with zeros to what it states rather than
    being required to carry them: an encoder that wrote zero as a space has had
    those spaces stripped by whatever moved the message. A line *longer* than the
    length states is refused.
  - **The name is taken as a name and never as a path.** A `../` would write
    outside the directory the user picked, and `C:\DL\FILE.ZIP` is a name FTN
    mail has carried since there was FTN mail. Both separators are cut, and a
    block naming `.`, `..` or nothing is dropped.
  - **The names are a label**, in the mode dialog and then in the export box:
    they are the message's rather than the user's, so there is nothing to type
    over them and nothing to pick among them, and the Tab ring walks past them.
    Five at most, the last row counting whatever is left.
  - **A label cannot take an Enter, so a `Save` button stands under it**, the
    ring's third stop where a text export has its name box. It is centred by
    measuring rather than by a `filler()`, and a click on it acts rather than
    merely focusing it, as the forward dialog's answers do.
  - **Nothing is written over**, which is where the two modes part company: a
    decoded file has nobody to ask, those names not being the user's. Every name
    is looked at before any is written, so a name already taken stops the export
    with `file exists: <name>` rather than leaving the directory half filled.
  - **The directory is the same directory**, and `state.exportDirectory` is where
    the next export starts from whichever mode wrote last. `exportName` is not
    touched by a decoded export: it is the name a *message* was written under.
- **The menu behind the corner.** The reader and the editor carry a **menu
  button** in their top-right corner — `ui/menu_button.hpp`, the back button read
  from the other side: the same five columns, two rows and colors, with `≡` where
  the arrow is. It opens `ui/menu_dialog.*`, a modal column of framed buttons
  standing one under the next so their frames meet and the column reads as one
  list. Every button is `menu_buttons_width` wide, frame included — **the setting
  decides, not the labels**. The column stands clear of the box edge by
  `kMarginX`/`kMarginY`, and `dialog::surface()` fills those margins with it.
  - **A label is a glyph and a word, and `labelOf()` hands the two back apart** —
    `{"↗", "Fwd / Copy"}`, `{"⚲", "Nodelist"}`. The word is the half a
    translation replaces; the glyph says the same thing in every language.
    `labelLine()` puts them together for drawing, in a glyph column
    `iconWidth()` columns wide — the widest glyph in the menu that is up — so
    that the words all start in the same place. When the room runs out it is the
    word that is cut, ellipsis and all: the glyph is what a button is picked out
    of the column by, and it is the half that does not grow when the interface is
    translated.
  - **The glyphs are measured, never counted.** `≔` is one code point in one
    column, `𝒊` is four bytes in one, `⚠︎` is two code points in one and an emoji
    is one glyph in two — and how many columns any of them takes is the
    platform's `wcwidth()` to say, so nothing assumes a width and everything asks
    `displayWidth()`. The U+FE0E on `⚠︎` is the variation selector asking for the
    text form rather than the emoji one: a zero-width mark that
    `term::toGlyphs()` attaches to the glyph it follows, so it costs no column.
    Do not measure a label with `size()`.
  - `menu_button` is a `config::Visibility` answered by `AppState::shown()`, and
    both corners cross at `AppConfig::adaptiveUiThreshold` on every frame.
    `AppState::menuButtonShown()` — and the wrappers `readerMenuShown()` /
    `composeMenuShown()` — is the one place that answers whether a screen has the
    corner, and an empty command list counts as none. **The corner costs no
    row**: it stands in the two the title and the rule already take, which is why
    nothing subtracts it from a screen's `…Rows()`. It is held against the right
    edge by a `filler()` that is a **child of the title row itself** — an hbox
    does not carry a child's flex up to its own parent.
  - `AppState::MenuView` is the box while it is up: the commands copied out of
    the config with whether each can be run **decided once, as it opens**, on the
    message that was in front of the user then. `app_shell.cpp` answers it like
    every other modal and dispatches by `navigator.current()` to
    `message_read::runMenuCommand()` or `compose::runMenuCommand()` — the box is
    put away *first*, since most of those commands put a box of their own up.
  - Every command is one a key does as well. A command with nothing to do is an
    `Item` with `enabled` false — drawn in `dimmed`, its click swallowed rather
    than passed to the screen underneath, and stepped over by the arrows. Which
    those are is the screen's own judgement: in an empty area everything about
    the message on screen is dead, `new` and `nodelist` being what is left.

### Text, theme and colors

- **Layout is measured in terminal columns, by `term::stringWidth()`.**
  `ui::displayWidth()` wraps it, and `substrByWidth`/`truncateToWidth`/`padRight`/
  `padLeft`/`wrapText` all budget in those units — that is what `ui/text_layout`
  exists for. Counting bytes or code points is wrong for anything the renderer
  does not draw one cell wide: a CJK ideograph is one code point and two columns,
  a combining accent one code point and none, and Cyrillic pushes a table out of
  line. The measuring here must be the measuring the renderer does, so call into
  it rather than reimplementing a width table.
- The screens carry no outer margin. The rules span the full width; the list rows
  carry a column of margin on each side themselves, so the highlight on the
  current row covers them rather than starting a column in. The message body is
  flush, the scrollbar taking the rightmost column when shown.
- **Quoting.** `ui::quoteDepth()` decides whether a body line is a quote and how
  deep: optional one or two leading spaces, up to six letters of initials
  (Cyrillic included, so counted in code points), the `>` markers, and a
  mandatory space after them. Odd depths render gold, even ones amber. The depth
  is taken from the source line and copied onto every wrapped piece, or a long
  quote would lose its color halfway down.
- **Links** are found by `ui::findLinks()` and colored per run inside the body
  line, which is why a body line is an hbox of pieces rather than one text when
  it holds one. Only `http://`, `https://` and `ftp://` count: a schemeless
  `www.` or a bare domain would mean recoloring ordinary words. Kludge lines are
  not scanned.
- **The palette is one struct, `ui::theme::palette`.** No screen names a color of
  its own; a role not in `Palette` does not exist. Its fields are RGB numbers in
  the terminal's own 256-color palette — `term::Color` holds one, and there is no
  separate theme color type. It is a global **written once**, in `runApp()`
  before the screen opens, and only read afterwards; do not write to it anywhere
  else.
- **Two fills, and the terminal's own is neither of them.** `app_shell` paints
  `background` across the whole window with `text` on it, and every modal paints
  `dialog_background` with `dialog_text` over the box it has just cleared
  (`dialog::surface()`). A cell left in the default-constructed `term::Color` —
  "whatever this terminal draws with when nothing is asked for" — is a bug: on a
  light profile it is black on white in the middle of a dark screen.
  `clear_under` is the only thing that produces one, and nothing calls it
  without painting over it in the same breath.
- **A box has a palette of its own, and a dialog draws from it and not from the
  screen's.** `dialog_background`, `dialog_text`, `dialog_title`,
  `dialog_label`, `dialog_hint`, `dialog_field` and `dialog_flash`, plus
  `menu_button`, which is only ever drawn inside one. A new dialog reaches for
  those rather than for `text`, `table_header`, `header`,
  `footer`/`dimmed`/`kludge`, `input_field` and `animated_button_text`, which
  are the screen's counterparts and stay on the screen. The split is what lets
  `themes/ged_classic.cfg` put a light grey DOS window with black in it over a
  screen that is light on black: one role cannot be legible on both. A test
  loads both shipped themes and checks that nothing a box draws with is the
  color of the box — the fills it puts down over its own, `selection` and
  `dialog_field`, and what is written on each of them included.
- **A terminal with fewer colors than a theme asks for** gets the nearest it has
  (`nearestWithin`), and one already holding the entry gets it untouched — which
  is what makes `themes/ged_classic.cfg`, written in the sixteen ANSI colors,
  exact on a bare console. A terminal in *direct* mode is the other way round: it
  reads a color number as a triple, so the entry is expanded through
  `paletteRgb()` first. Skipping that is a silent wrong-colour bug, not a missing
  optimisation — index 102 would go out as #000066. Colour pairs are allocated as
  first asked for, so the count follows the theme rather than the roles.
- **Adding a color role means three edits**: the field in `Palette`, the line in
  `kFields` in `ui/theme.cpp` tying it to its theme-file key, and an entry in
  every file under `themes/`. Tests load both shipped themes — `default.cfg`
  against the defaults field by field, `ged_classic.cfg` for the opposite, that
  no field was left at a default — so forgetting a file fails the build. The one
  role exempt from that opposite is `hint_bar`: both themes state the same dark
  grey, deliberately the default's own, so that the row along the bottom does not
  change shade with the theme.
- **The BBS color codes are markup taken out of the text; the style codes are
  markers left standing in it.** `bbs_codes_renegade` turns on the
  Renegade/Telegard pipe codes `|00`–`|31`: `ui/bbs_codes` reads them, the first
  sixteen naming a foreground and the rest a background, in the DOS color order
  rather than the terminal's (`kDosToTerminal` — blue and red change places, and
  so do cyan and yellow). `|24`–`|31` are what a DOS adapter drew as either a
  bright background or a blinking foreground, and they are taken as the
  background: nothing in a reader should flash. Three things follow from the
  codes being taken out:
  - **They are stripped before the body is wrapped**, in `wrapBody()`, not while
    it is drawn. A code is three bytes and no columns, so a line measured with
    one in it wraps early, and `quoteDepth()` would not find a marker behind one.
  - **A color crosses a wrap and stops at a newline.** One break is the window's
    doing and must not change what the message looks like, the other is the
    message's own. So `stripRenegade()` reads one line at a time and carries
    nothing between them — every line opens in the theme's colors, which is what
    keeps the quote colors, the trailer and the kludges the reader's after a
    message opens a color and never closes it — while `runsForRows()` cuts a
    line's runs up between the pieces `wrapText` made of it and opens each in the
    color the break fell under. A row carries that opening color in
    `DisplayLine::colorRuns`, because the reader draws only the rows on screen
    and one that could not say what it opened under would lose the color when a
    long line was scrolled into from the middle. The rows are found in the text
    rather than tracked through the wrapping: the layout must not depend on
    whether the config asked for colors.
  - **Off is the default and per-area is the point.** A pipe is an ordinary
    character in every echo not written this way. What the message says nothing
    about keeps the theme's colors, so an uncolored quote is still a quote.

### Charsets and the locale

- **Charset resolution**: the `CHRS:`/`CHARSET:`/`CODEPAGE:` kludge, then
  `default_charset`. Fidonet names are mapped onto iconv names in
  `charset_detector.cpp` — `+7_FIDO` and `866` both mean CP866.
- **Reading and writing have separate settings, both required.**
  `default_charset` is only ever a fallback for a message being read;
  `compose_charset` is what a new message is encoded in and what its CHRS
  announces, and it is the only thing `message_builder.cpp` reads. Neither has a
  default: a guess would be a silent mojibake in whichever direction it guessed
  wrong, so `fromEntries()` fails when either is missing, alongside the check
  that there is an area list at all — `tosser_config`, `area ... endarea`
  blocks, or both.
- **`IBMPC` is not CP866**, however often it is one in practice. FTS-5003 keeps
  the name only as an obsolete level-2 one meaning "some IBM PC code page":
  CP866 in Russian echoes, CP437 or CP850 in western ones, CP852 in central
  Europe. `normalize()` returns an empty string for it, as for a value that is
  not there at all, and `detect()` falls back to the default. Mapping it to CP866
  silently mojibakes every western area that writes it.
- **Both are per-area, and an area group is where that comes from.** No tosser
  config format states a charset — husky fidoconfig has no `-charset` option
  (grep its sources and `doc/`: the word does not appear), and neither do
  areas.bbs or squish.cfg. A `group ... endgroup` block answers it, and it
  arrives through the constructor: a `CharsetDetector` belongs to one open
  `FtnMsgBase`, and `AreaManager` builds that base with
  `effectiveFor(area).defaultCharset` at all three sites where it builds one.
- **A header is decoded in the charset its own body declares.** The names and
  subject sit in XMSG, but the CHRS kludge saying what charset they are in is
  part of the body, so `readHeader()` reads the control block (`readKludges()`)
  and asks the detector with that. Skipping it means the message list shows
  subjects in `default_charset` while the reader shows the body in the charset it
  declares. `testdata/msgbase/charsets` exists for this: the same word in KOI8-R
  and CP866 with matching kludges, and one message with no kludge at all.
- **Everything above the adapter is UTF-8, and the terminal layer encodes on the
  way out.** A cell reaches ncurses as `wchar_t` through `setcchar`, and ncurses
  writes it in whatever `LC_CTYPE` names — so an 8-bit terminal is supported by
  setting the locale to match it (`LC_CTYPE=ru_RU.KOI8-R`) and nothing else. Do
  not reach for `iconv` for this. `Terminal::codeset()` says what the locale
  settled on and nothing reports it to the user.
- **A file on disk that declares nothing is read in the locale's charset.**
  `encoding::localeCharset()` is `LC_CTYPE` from the environment, and the one
  thing that asks is an `echolist` line stating no charset of its own. It is not
  a fallback for a *message*: a message declares its charset in a CHRS kludge and
  falls back on `default_charset`, and neither has anything to do with the
  terminal's.
- **The locale is not left to chance.** `term::ensureUtf8Locale()` runs before
  ncurses starts. A locale the user chose is kept, whatever its charset; where
  the environment names none, or names the C locale, a UTF-8 one is found and
  installed — under the C locale `wcrtomb()` would drop every non-ASCII character
  on the way out and `wcwidth()` would refuse to measure one. A VPS with no
  locale set is the normal case. The consequence: **`<cctype>` must not be
  trusted for case or whitespace**, since a single-byte locale like KOI8-R folds
  the high half of the byte range and would make two differently spelled Cyrillic
  names compare equal. Use `text::asciiLower`/`asciiIsSpace` and the local
  equivalents in `domain/` and `encoding/`.

### Config and area groups

- **AmberEdit's own config and the themes are one format**, read by
  `config/cfg_file`: a line is a key and the values after it, double quotes round
  a value whose spaces matter, a `#` starting a word ending the line. No
  sections, and no types beyond what reading a key asks for —
  `CfgEntry::text/one/number/numberIn/flag`, each answering with a `Result` that
  names the file and the line. Adding a setting is a branch in `fromEntries()`
  (`config/app_config.cpp`), and a key not in it is refused rather than passed
  over: a misspelling should be a message and not a setting quietly back at its
  default. Keys are lowercased on the way in, values never. The two shapes a file
  written for the old toml format still has — a `[section]` header and
  `key = value` — are named for what they are rather than read as odd values.
  `group ... endgroup` is read out of the flat list by `app_config.cpp`, not by
  `cfg_file.cpp`, which the themes share and where a block would mean nothing.
- **`tmpdir` is optional, and `config::makeTempDir()` is the one place that
  knows why.** Whoever needs somewhere to work calls it with `cfg.tempDirPath`
  and gets a directory made and ready: the setting where it names one, and
  `amberedit-<uid>` under `std::filesystem::temp_directory_path()` (`$TMPDIR` or
  `/tmp`) where it does not. Not resolved while the config
  is read, which parses without standing in for the machine it will run on — the
  same reason the template is read in `loadFromFile` and not in `fromEntries` —
  and not made until it is wanted, so a config naming one it never uses leaves
  nothing on the disk. A directory we fell back on is checked before it is used
  (no symlink, permissions ours to set), the system's temporary directory being
  shared with everybody logged in and the names written there not ours to
  choose; one the config named is used exactly as the user made it. The message
  says what is wrong with the directory, and the caller adds what it wanted one
  for. Today the one caller is `NodelistSources::readArchive`.
- **An area group is per-area settings, and it is not the tosser's group.**
  `domain::AreaConfig::group` is a label fidoconfig's `-g` prints in a column; a
  `config::AreaGroup` is a block of AmberEdit's own config, matched on the
  echotag by `config/area_pattern` (`*`, `?`, ASCII case folding).
  - **The chain is `applySetting()`**, and a group keeps the `CfgEntry`s it was
    written on and runs them through it again — which makes a new setting one
    edit rather than a field, a parse and an apply that drift apart. It also
    means a group is *applied once while the config is read*, over a copy that is
    thrown away, so a bad value stops AmberEdit at startup and `effectiveFor()`
    can never fail — which is why it discards the answer rather than passing it
    on, and why its 43 call sites have nothing to check.
  - **What a group may say is a whitelist** (`isGroupSetting()`), not a list of
    what it may not: a setting added to the chain and not to the table comes out
    as "not a per-area setting", where the other way round it would come out as a
    layout key silently overridable per area.
  - **Groups merge setting by setting**, most specific last — literal characters
    before the first wildcard, then in the whole pattern, then no wildcard at
    all. Two patterns that rank equally, cover some tag in common
    (`AreaTagPattern::overlaps()`, a small DP) and state the same setting are
    refused at startup; the check is made against the patterns rather than the
    areas so that it does not wait for the day an echo trips it.
  - **Three configs reach the screens**, and which to read is one question — may
    a group state this setting? `AppState::config` is the file as read, for
    everything a group may not touch; `AppState::areaConfig` is it resolved for
    `currentArea` (`setCurrentArea()` is the one place either is assigned);
    `AppState::composeConfig()` is it resolved for `composeArea()`, which is not
    the same area for a moved reply or a forward. Reading a groupable setting off
    `config` works everywhere except in the areas a group covers.
- **`area ... endarea` declares an area AmberEdit's own config owns.** The same
  fields a tosser states one with — `path`, `type`, `kind`, `description`,
  `group_label`, `address`, `link` — read into a `domain::AreaConfig` by
  `readManualAreas()` and kept as `config::ManualArea` (the area and the line
  the block opened on). Everything above `IAreaConfigSource` is unchanged: an
  area is an area whichever file declared it, so groups, the AKA fallback, the
  counts and the base creation all reach it as they always did.
  - **The list is the tosser's areas and then the blocks**,
    joined by `config::ManualAreaSource`, which wraps the tosser's parser rather
    than standing beside it: `AreaManager` knows one source. The wrapper is
    skipped where there are no blocks, and the tosser's parser is null where
    there is no `tosser_config` — a config may be all blocks, and
    `fromEntries()` then insists only that it be *something*, one of the two.
  - **A tag declared in both is refused where the two lists meet**, in
    `ManualAreaSource::loadAreas()` and not while the config is read: the
    tosser's config has not been opened by then. Which is why `ManualArea` keeps
    a line number — it is all the complaint has to name the block by.
  - **`group_label`, not `group`**, for what fidoconfig's `-g` sets. The block
    stands in a file where `group ... endgroup` already means area settings, and
    one word for both would be the confusion this section warns about, written
    into the config language itself. `address` *is* reused: inside a group block
    it already means the area's AKA, and it means the same here.
  - **Nothing about a block looks at the disk.** A path with no base under it is
    ordinary — declaring an area before its base exists is what someone writes a
    block for, and `openArea()` creates it on the way in, which needs the `type`
    the block states (`FtnMsgBase::isAbsent` answers "not absent" for
    `Unknown`). Left out, the type is probed from the files instead.
  - **`splitBlocks()` takes both blocks out of the flat list** in one pass and
    refuses either inside the other; `readManualAreas()` and `readGroups()` then
    read what it collected, both after the file's own settings, so an `address`
    a block states can be added to the AKAs beside an `address` line below it.
- **Tosser config formats**, all three stated explicitly in AmberEdit's own
  config and never sniffed: fidoconfig (hpt-style) uses `EchoArea` /
  `NetmailArea` / … lines with spaced options (`-b squish`, `-g A`,
  `-a 2:5020/1` for the area's AKA); areas.bbs is line-based, the path prefix
  naming the base type (`$` Squish, `!` JAM, none Fido `*.msg`) with a bare `P`
  meaning passthrough; squish.cfg uses `EchoArea` / `NetArea` / … lines whose
  options carry their value **attached** — `-$` for a Squish base (absent means
  Fido `*.msg`), `-$gA` for the group, `-p2:382/736` for the area's AKA, bare
  addresses after them being links. Squish cannot describe JAM at all.
- **`valueOptions()` in `fidoconfig_parser.cpp` must match husky exactly.** The
  parser skips any `-option` not in that set, so listing one husky treats as a
  flag makes it eat the token after it — a stray `-pack` would swallow
  `-b squish` and leave the area with no base type. The authority is
  `parseAreaOption()` in `fidoconf/src/line.c`. A value is never allowed to start
  with `-`, as a second line of defence if the two drift apart.
- **An area's AKA is not a link.** Both fidoconfig's `-a` and squish.cfg's `-p`
  name the address the area is presented under and take exactly one; the bare
  addresses that follow are the links. Reading `-a` as a list of links silently
  turns the sysop's own address into a downlink.
- **An area's AKA is the area group's, then the tosser's, then the config's.**
  Only fidoconfig and squish.cfg can state one per area, and in both the option
  is optional, so `AreaManager::reload()` fills an unset address in from the
  config's `address`; areas.bbs never states one, which makes that fallback the
  common case. An area group naming an `address` beats the tosser outright, and
  every address any group states is also added to `akaMatches` with no patterns,
  so `isOwnAddress()` knows a message written under one as the user's own while
  `akaMatching()` never picks it by destination. With no address anywhere the AKA
  is left out of the titles.

### Dates

- **Every date is written through `MessageDate::format()`**, with a strftime
  format from the config: `reader_datetime_format` wherever a stamp is read, and
  `template_date_format`/`template_time_format` for the template's
  `@cdate`/`@odate` and `@ctime`/`@otime`. There is deliberately no fixed-width
  spelling: **every column showing a stamp measures it** — the reader's header
  from the two stamps and the attributes, the message list from the rows on
  screen — so a format may be any length. **The stamp is trimmed at both ends**,
  because a specifier that writes nothing leaves the space beside it behind and a
  column measured off a stamp ending in a blank is wider than what stands in it.
  `readTimeFormat()` refuses a format that writes more than a line (`%n`, `%t`)
  or nothing but blank; it checks against a sample stamp that does state a zone,
  so `%z` alone is not refused for the blank it leaves elsewhere. The fields go
  to strftime as the base stores them — an FTN stamp is in no time zone, so `%Z`
  says nothing and `%a`/`%j` are worked out from the date itself rather than
  through `mktime()`, which would answer in the local zone. They are clamped on
  the way in: strftime indexes its own tables by them and a base can hold
  anything.
- **`%z` is answered from the message, not from strftime.** The only thing that
  says which clock an FTN stamp is on is the message's own TZUTC control line, so
  `format()` takes the zone as its second argument and substitutes it before
  strftime sees a `%z` — glibc would otherwise answer out of `struct tm` and have
  every message written in UTC. `msgbase::tzutcOffsetOf()` reads that line out of
  the control block (`TZUTCINFO` too, FTS-4008 §3) and writes it the way `%z`
  does, with the sign FTS-4008 leaves off a positive offset;
  `FtnMsgBase::header()` puts it in `MessageHeader::utcOffset`, out of the
  control lines it reads for the charset anyway, so the message list's Date
  column costs no second read. A message stating no zone gets an empty string.
  **Only the written stamp is given one**; the arrival stamp is passed no zone at
  all on either screen, having been read off this system's clock.

### The header block and adaptive layout

- **The header block has one layout, and the one setting for it is a row.** Six
  fields on `AppState::kHeaderRows` (4) rows and two columns, whatever the
  window: the names down the left, hard against them the addresses, then the
  attributes, then the written stamp on a Date row of its own. Nothing stands
  between the two columns in either screen — the name cell is padded to its
  width, so where it ends is where the address begins. The left column is as wide
  as the widest thing in it: names stop at `kMaxNameWidth` (36, the FTS-0001
  field), and a stamp may ask for more, in which case the addresses move over
  with it — which is why `headerLayout()` takes the width it wants rather than
  capping everything at `kMaxNameWidth`. The window still decides what is cut.
- **The rightmost column holds either the stamps or the attributes**, so it is
  reserved whenever there is anything for it, not just when a stamp exists.
  Sizing it on the stamps alone leaves a message that has none no room for its
  attributes, the layout squeezes the cells to fit, and the columns silently stop
  lining up.
- **The arrival stamp has a row of its own, and `show_recd_date` says whether
  there is one.** `Recd : 14 Aug 26 20:15` under the Date row, drawn exactly like
  it, label and stamp in the block's own `header`. `AppState::recdRowShown()`
  answers the setting and `AppState::headerRows()` is `kHeaderRows` plus that row
  — **both screens count their chrome from `headerRows()`**, never from
  `kHeaderRows`, or the block and the text under it come apart. The row does not
  come and go with the message: one written here arrived from nowhere and the row
  is simply blank, a block that grew and shrank message by message walking the
  body up and down the window while reading through an area.
- **The editor's block is the reader's, field for field**, its Date and Recd rows
  standing where the reader's do and showing the clock, read afresh every frame:
  a message being written is written now and arrives here now. The stamps are
  `app::localStamp()`'s at the moment the message is stored, and neither row is a
  stop in the ring the cursor walks.
- **A window of `AppConfig::adaptiveUiThreshold` columns or more is wide**, and
  that is the line the whole interface adapts on. It is asked on every frame
  (`wideWindow()`, `AppState::shown()`) rather than settled at startup, because a
  window can be dragged and the config is read off `AppState::config` each time.
  Every `config::Visibility` setting — `menu_button`, `back_button`,
  `show_recd_date` — is answered by `AppState::shown()`, the one place that reads
  a `when_narrow` / `when_wide` from either side. Write the width as the setting,
  never as a literal 80.
- `FtnMsgBase::open()` confirms the base is on disk with `probeType()` first.
  That one is for the message: the driver would refuse a missing base fine, but
  its error would not say which format was expected, and squish.cfg reaches the
  case easily, `*.msg` being its default.

## The message base drivers

`src/msgbase/` reads and writes the three formats itself; there is no
third-party message base code and no submodule. The layering inside it:
`BinaryFile` (a descriptor, pread/pwrite at offsets), `FileLock` (fcntl locks
over every file of a base), `byte_order`/`raw_message`/`jam_crc32` (the encodings
the formats share), then one `FormatDriver` per format — `SquishBase`
(.sqd/.sqi, FSP-1037), `JamBase` (.jhr/.jdx/.jdt, JAM-001), `SdmBase` (N.msg,
FTS-0001 with the Opus header) — and `FtnMsgBase` on top, the one `IMsgBase`
implementation, where charsets are converted and lines are marked.

**Every write, change and delete locks the base's files first and releases them
after** — `.sqd` and `.sqi` for Squish, `.jhr`, `.jdx` and `.jdt` for JAM —
through `FileLock`, whole files, all-or-nothing, with the state the write depends
on (counters, free chains, file lengths) re-read under the lock. A tosser may be
writing the same area between two keystrokes. Fido `*.msg` has no base files to
lock for a *new* message: a message is a file of its own, created `O_EXCL`, and
the loser of a race over a number rescans and takes the next. Rewriting one is
locked all the same — the file is the message.

**An address the header is short of is completed from the kludges** —
`completeAddresses()` in `raw_message.cpp`, called by the Squish and Fido `*.msg`
drivers on the control lines they have read anyway. The zones come from `INTL`,
the points from `FMPT` and `TOPT`. Fido `*.msg` has no field for either; a Squish
XMSG has words for all four and a great many tossers write netmail with the zone
words left at zero, FSC-0004 having made the kludge the place a zone is stated —
without this a netmail area reads as `0:5059/38` and a reply goes nowhere. Only a
field the header leaves at **zero** is answered for. `INTL` is read the guarded
way: one whose net and node are not the header's belongs to a message this one
was routed inside of. Fido `*.msg` alone falls back to the area's own zone where
no kludge says one; Squish leaves the zero, its header being the place a zone was
meant to be. `info()` shows the stored words either way — that screen is a dump
of the record, not of what was made of it.

**Fido `*.msg` is the Opus header, not FTS-0001's.** The 190 bytes a message file
opens with are: `from` at 0 and `to` at 36, both 36 bytes; the subject at 72, 72
bytes; the ASCII date at **144**, 20 bytes; times-read at **164**; then
`destnode`, `orignode`, `cost`, `orignet`, `destnet`, a word each; then **eight
bytes at 176 that are a union** — FTS-0001 puts `destzone`, `origzone`,
`destpoint` and `origpoint` there, Opus put the written and the arrived stamp, a
DOS date word then a DOS time word each; then `replyto` at 184 (which is also
where `1.msg` keeps an echo's high-water mark), `attr` at 186, `reply1st` at 188,
and the text from 190, NUL-terminated. Those offsets are named constants in
`sdm_base.cpp` and are worth reading twice before touching: a date written
thirty-six bytes early lands *inside* the subject field and leaves offset 144
zeroed, where every other FTN program looks for it.

AmberEdit reads and writes the **Opus** half, which is what GoldED+ does unless
its `FIDOMSGTYPE` says otherwise and what its manual calls the dominant variant —
`goldlib/gmb3/gmofido.h`, `struct FidoHdr`, is the layout written out. There is
deliberately **no setting and no sniffing** for the other reading: nothing in the
bytes tells the two apart, and a guess there would silently change a netmail
*address*. Nothing is lost by not having one — the zones come from `INTL` and the
points from `FMPT`/`TOPT`, and a header written the FTSC way simply has no stamps
and is dated by its ASCII date.

**A Fido `*.msg` header has both stamps and often fills in neither.** `SdmBase`
reads both halves of the union at 176, but plenty of writers leave them empty and
state only the ASCII date, and a header written the FTS-0001 way has zone and
point words there instead. Then the written stamp falls back to that date and the
arrival stamp to the written one, so an SDM message shows the same time on both
header rows. Squish and JAM keep the two apart. Nothing to fix — just do not read
equal stamps in a `*.msg` area as a bug.

**`create()` makes an empty base and nothing else.** It is what entering an area
the tosser config declares but nothing has written into does. Per format: a
256-byte area header alone in the `.sqd`, its `end_frame` at the end of that
header (the one field `readBaseHeader()` refuses a zero in, which is how a file
of zeroes left by a tosser that died mid-creation is told apart from an area
deliberately empty) beside a zero-length `.sqi`; a 1024-byte JAM info block with
the signature, `basemsgnum` 1 and no password, beside a zero-length `.jdx` and
`.jdt`; a directory for Fido `*.msg`. Every file is created `O_EXCL`, and the
file the type is probed by — the `.sqd`, the `.jhr` — is made **last**, so a
creation interrupted half way leaves files no base claims rather than a base
missing what it is read through. Whatever was made is removed again when a later
step fails. The driver is not left open on what it made: creating a base and
reading one are two steps.

**`replace()` disturbs the base as little as the format allows.** No other
message moves and nothing is copied up or down the base — a message changed at
the head of a large area would otherwise cost the whole of it:

- *Squish* rewrites the message inside the frame it already owns whenever it
  still fits, the frame marked `kFrameUpdate` while it is half written (which is
  what `read()` already answers "another task is updating this" to). One that has
  outgrown its frame takes a frame off the free chain or one at the end of the
  file, is linked into the chain where the old one stood, and the old frame goes
  back on the free chain. The index record — one 12-byte write — says where the
  message now is; its UMSGID never changes.
- *JAM* writes the text back over itself where it still fits and at the end of
  the `.jdt` where it does not, and the header in place only when the subfields
  take **exactly** the room they took, since what stands after a header in the
  `.jhr` is the next message's. Otherwise a new header goes at the end, the index
  record is repointed at it and the header left behind is marked deleted for a
  packer. The index record's position is the message number, so the number — and
  the UID with it — survives either way.
- *Fido `*.msg`* rewrites the file and truncates it to what the message now
  takes. The file name is the number, so nothing else is touched.

What the draft decides is the header fields, the control lines and the text. What
the *stored* message keeps is its UID or number, the stamp it arrived here under
and its thread links — read back off the message being written over rather than
taken from the draft. The date it is **written** under is not kept: a message
written again is written now, so `FtnMsgBase::replace()` stamps it exactly as
`write()` does and `TZUTC` is rewritten with it (`app::buildChange()`, the one
control line a change touches). That is why `FtnMsgBase::encode()` leaves both
stamps empty and each of the two callers fills in what it means.

**`info()` is the one call that is about the storage rather than the message.**
It answers the reader's `i`, and each driver answers with its own fields, there
being nothing in common between a Squish frame and a JAM subfield worth
pretending there is:

- *Squish* — the XMSG header (names, subject, both stamps as the date they stand
  for and as the dword they are packed into, the UMSGID and all nine reply
  slots), then the Message Base Record, the Message Index Record and the Message
  Frame Record, then dumps of the XMSG, the control block and the text.
- *JAM* — the fixed header, the index record, the base header, and **every
  subfield one line each**, under the number and the name JAM-001 gives it: that
  is where a JAM message keeps what the other two put in a header field or a
  kludge. Then dumps of the header, the subfield block and the text.
- *Fido `*.msg`* — the file and its size, the 190-byte header field by field, the
  eight bytes at 176 read the **Opus** way, then dumps of the header and body.

**The field names are GoldED+'s**, from `make_dump_msg()` in its `goldlib/gmb3`
(`gmosqsh5.cpp`, `gmojamm5.cpp`, `gmofido5.cpp`): a report read here and one read
there are meant to be the same report. Where AmberEdit knows something GoldED+
does not show — the frame's type, JAM's FTS-0001 reading of the attribute word —
it is added after the common fields and said to be an addition in a comment.

Three rules hold across the three drivers. A message that cannot be read comes
back empty rather than half filled in. A dump is capped at
`report::kMaxDumpBytes` and a block cut short says so in its own title, so a
message somebody attached a file to cannot cost the interface a frame. And the
values that are **text out of the message** — names, subjects, subfields — are
marked `MessageInfoField::text`, which is what `FtnMsgBase::info()` converts out
of the message's charset into UTF-8; numbers and offsets are ASCII in every
charset and are handed on untouched, and the dumped bytes are never converted.
`msgbase/info_format.*` is where the spelling of a value lives — hex with the
decimal beside it, an attribute word with its bits — so the three drivers cannot
drift apart.

When the old smapi implementation (huskyproject/smapi, the reference this code
was written against) and the format specifications disagree, the tests and the
checked-in fixtures are the authority: `testdata/msgbase/localnet.*` was written
by smapi and must keep reading correctly, and what AmberEdit writes must read
back through its own drivers and through any other FTN software.

## The nodelist

`src/nodelist/` reads FTS-5000 nodelists and compiles them into one binary file,
which `ui/nodelist_dialog` then searches. Its own library because zlib is wanted
for it and for nothing else, and because nothing in the core knows it is there.
Three config lines: `nodelist`, `nodelist_db` and `tmpdir`.

**Compiling happens at startup, and only when it is needed.** `main.cpp` calls
`refreshNodelist()` before the terminal is taken over — the only place left to
say anything about it — and that compares what each `nodelist` line names *now*
against what the compiled file says it named *then*: the path, the modification
time and the length, written into the file as a `SourceState` per line. A listing
and a stat per line, nothing read and no archive unpacked. `--compile` compiles
anyway.

**Nothing in the compiling fails as a whole.** A nodelist that is missing or will
not read is a line in `CompileReport::problems`, and its `SourceState` is
written into the compiled file as the nothing it was — which is what stops every start from trying
it again. A compiled file that cannot be written leaves `written` false. That
contract is the reason `compileNodelists` catches around each source and around
the write: AmberEdit is a mail reader whose nodelist is a convenience, and there
is no version of "your nodelist is missing" worth standing between the user and
their mail.

The pieces, in the order the work goes through them:

- `NodelistSpec` / `stateOf` / `NodelistSources` (`nodelist_source`) — what a
  `nodelist` line names: a filename, a day-number pattern (`Z2DAILY.999`) or a
  zipped one (`Z2PNT.Z99`). **The extension names the kind and the rest of the
  filename is a glob.** `.*` and `.Z*` are those two patterns written as
  wildcards and mean exactly what `.999` and `.Z99` mean; a wildcard against any
  other extension leaves the kind `Exact` and globs the whole filename.
  Directories are never globbed — a pattern over them would be a pattern over
  whose nodelist this is. **Newest is the modification time, with the higher
  number breaking a tie**, and the later filename after that, so that two files
  a loose stem matched cannot depend on the order a directory listing handed
  them over in — the number alone is wrong for the week after New
  Year, when `.365` is the older file and the larger number. An archive is
  unpacked without paths into the temporary directory, only the entry carrying
  the nodelist, and every file it wrote is removed by the destructor, whichever
  way out is taken. Which directory that is is
  `config::makeTempDir`'s answer and not this class's — see below. **What stands
  inside an archive is named by the archive that was found, not by the line that
  found it**: `Z2PNT.Z99` and `z2*.z*` both land on `Z2PNT.Z19`, and `Z2PNT` with
  a day number after it is what is looked for inside either way.
- `archive::ZipArchive` (`archive/zip_reader`) — enough zip for a nodelist or an
  echolist: stored and deflated entries over the system zlib, the CRC checked.
  Zip64, encryption and multi-part archives are refused by name rather than
  half-read. It belongs to neither subsystem, which is why it stands outside
  both.
- `parseNodelist` (`nodelist_parser`) — the lines. **Nothing declares whether a
  file is a nodelist or a pointlist**: `Zone`, `Region` and `Host` set the net
  the lines under them are in, `Boss` sets the node the lines under *it* are
  points of, and a file holding both reads correctly for that reason. A line that
  cannot be read is a `ParseWarning` against its number, never a guess. The DOS
  end-of-file mark (`^Z`) ends the file, which is what the real ones carry.
- `writeNodelistDb` (`nodelist_writer`) and `NodelistDb` (`nodelist_db`) — the
  compiled file, written beside its destination and renamed over it, so a reader
  never sees half of one.
- `refreshNodelist` / `nodelistNeedsCompiling` (`nodelist_compiler`) — the two
  entry points `main.cpp` uses, and the whole of the "when".

**The format is `nodelist_format.hpp`, and its layout is documented there.** Two
searches decide it:

- An address, whole or in part. Every node is keyed by one 64-bit
  zone:net/node.point, so the numeric order of the keys is the order an address
  reads in, and *every* partial address is a contiguous run of the sorted index —
  `2`, `2:382`, `2:382/736` are the same binary search with a shorter key.
  `AddressPrefix` is what a typed one parses to; a trailing separator is allowed
  so a search field can be read while it is still being typed.
- A sysop's name, whole or in part. The folded names are a pool and the name
  index is a **suffix array** over it — every position of every name, sorted by
  the text that follows. Matching inside a word is what that buys, and it is what
  "partial match" is usually taken to mean.

`format::kVersion` is written into every file and checked when one is opened; a
file from another version is refused, and `nodelistNeedsCompiling` reads that
refusal as "compile it again". **It goes up whenever a file written today would
be misread by the code that reads it** — never for a field added past the end of
the header, which `headerSize` already accounts for.

Three more things before changing any of it:

- The three fields a nodelist writes spaces in as underscores — the system, the
  location and the sysop — are stored with the spaces back in them; `toLine()`
  puts the underscores back. The phone and the flags are kept exactly as written.
- Where two entries stand at one address, **the first source named wins**, and
  within one source the first line does. The config's order of `nodelist` lines
  is the only statement of precedence anybody has made.
- A nodelist is ASCII by FTS-5000 and a few of them are not — a Latin-1 byte in a
  Scandinavian name. Nothing decodes them: the byte reaches the terminal layer
  and is drawn as its replacement glyph. There is no charset to read one by.

`ui/nodelist_dialog` is what a user sees of all this. What was typed into the
Lookup line is an address when it parses as the beginning of one and a sysop's
name otherwise — no sysop is called `2:240`. The head of the box is the node
under the cursor, line for line as the nodelist has it, and the bottom of the
frame names the file it came from (`sourceAt()` and the `SourceState` behind it).

**`show_location`** puts the sender's location, as the nodelist gives it, into
the rule that closes the reader's header block — lined up under the addresses, in
the kludges' colour, costing no row since the rule is there either way. A sender
no compiled nodelist holds leaves the rule exactly as it was. That is also what
opens the compiled file in an ordinary session, so `AppState::nodelist()` is
where the lazy open lives rather than in the box.

**A point no nodelist here lists is answered for by the node it hangs off** —
`NodelistDb::findOrBoss`, which both the location and Ctrl-N go through. A pointlist
is a separate file most systems never compile, so a point is the address
likeliest to be missing and its boss is the one thing certainly known about it.
Ctrl-N opening on a point that fell back puts the boss's address in the Lookup line,
since that is what the box is showing. **The compose screen does not fall back**:
`useNode` addresses a message to the node that was picked.

The box opens for three things, and `NodelistView::Purpose` says which:

- **Ctrl-N or F10 in the reader** (`Browse`), on the sender of the message on
  screen. The
  list is the whole nodelist and the Lookup line is where the cursor goes; it
  deliberately does not filter, because a node is worth as much for its
  neighbours as for itself.
- **A To name with no address under it** (`PickAddress`), from the compose
  screen. Here the list *is* what the name found, **closest first** — somebody
  who typed a name is asking which node is theirs. A name that is an
  `address_macro` never reaches this.
- **A To address with no name above it** (`PickName`). The list is the whole
  nodelist at that address, as the reader's own box shows it.

The two that pick answer `Outcome::Picked` and leave the box standing, so the
shell can take the node off it — the area picker's shape exactly.
`compose::useNode` fills in **both halves from the node**, whichever was asked
about: half a row out of the nodelist beside half a row out of a search field is
not that node. The name goes in as the nodelist spells it — `Schroeter` is how a
node is found and `Ulrich Schroeter` is who is there, and that spelling is what
the system at the other end matches on. The cursor comes to rest on the subject.

Closest first is `NodelistDb::SysopOrder::Relevance`: the whole name, then the
name the query begins, then a word of it, then the middle of a word — shorter
names first inside a rank, address order under that. A limit is applied after the
order and never before it, so it is the best `limit` of them.

It is a modal of a fixed size, measured by `fitBox()` as the import and export
boxes are. A list longer than the box carries the reader's own scrollbar
(`ui/scrollbar`, a cell at a time) in the rightmost column inside the frame, and
no bar at all where the whole list fits. In a box too narrow for the address and
a whole sysop's name, **the station column goes rather than all three being
cut**. Backspace edits the Lookup line and never closes the box — the two are a
keystroke apart while a lookup is being cleared to type another, and Esc is the
way out. The address the box opened on is `seeded`: the first character typed
takes the whole line with it, and only the first, since an address with a letter
added to the end looks up nothing at all.

## The echolist

`src/echolist/` reads echolists and compiles them into one binary file, whose
descriptions are then laid over the area list. Its own library beside the
nodelist: the two share the *shape* of the work — a source that may arrive
zipped, a compiled file with a state per source in it — and nothing else, and
neither knows the other exists. Four config lines: `echolist`, `echolist_db`,
`arealist_description_priority` and `tmpdir`.

**An echolist is read for the description and for nothing else.** The tag and
what an echolist says about it are the whole of what is kept; the status, the
moderator and their address that a `.lst` line also carries are for other
programs. That is why the compiled file has one index and no other order, and why
nothing displays an echolist — there is no browser and no key, and a description
turns up in `AreaConfig::description` as though the tosser config had carried it.

**Compiling happens at startup on the nodelist's terms exactly**: `main.cpp`
calls `refreshEcholist()` before the terminal is taken over, the state written
into the compiled file is compared against a stat per line, `--compile` compiles
anyway, and **nothing in the compiling fails as a whole**. Read
[The nodelist](#the-nodelist)
for the reasoning; all of it holds here word for word.

The pieces, in the order the work goes through them:

- `stateOf` / `EcholistSources` (`echolist_source`) — what an `echolist` line
  names: a file, or a `.zip` holding several, either of them named outright or
  by a glob over the filename (`echo*.zip` — the newest match wins, by
  modification time and then by the later name, so that a name carrying its
  month settles it in the order such names sort in). There are deliberately
  **none of the nodelist's sentinels** — no `.999` and no `.Z99` — because an
  echolist arrives under a name that changes every month rather than every day,
  and a wildcard says that without a second vocabulary to learn. **Whether what
  was found is an archive is its own name's to say**, not the line's, so one
  pattern may cover both kinds and reads whichever it landed on. An archive is
  unpacked without paths into the temporary directory, **only its `.lst` and
  `.na` entries** (a distribution carries reports, a readme and further archives
  beside them), in the order their names sort in so that an archive read twice
  reads the same way twice. Every file it wrote is removed by the destructor,
  whichever way out is taken.
- **The charset is settled here and never survives it.** An `echolist` line
  states the charset the file is written in and the text is decoded to UTF-8 as
  it is read, so `parseEcholist` and everything past it see UTF-8 like the rest
  of the program. A line that states none is read in `encoding::localeCharset()`,
  which is `LC_CTYPE` and nothing cleverer: a guess in its place would be a
  silent mojibake in whichever direction it guessed wrong. **The charset is part
  of the `SourceState`**, so correcting a line recompiles the file it names
  though the file has not moved.
- `parseEcholist` (`echolist_parser`) — the lines. **Two shapes, told apart by
  the extension**: `.na` is the tag, blanks, and the description to the end of
  the line, and everything else is the comma-separated
  `[Status],Tag,Comment,Moderator's Name,Address,`. The comma-separated one is
  the general shape and so the default; the two-column one has to be asked for by
  name. The format has no quoting and no escape, so a comma in a description is
  where that description ends — a line whose author cut their own description
  short, and not something to guess around. **An echo with an empty description
  is not an entry**: it carries nothing this file is read for, and a `.na` list of
  bare tags is an ordinary file rather than a mistake to warn about.
- `writeEcholistDb` (`echolist_writer`) and `EcholistDb` (`echolist_db`) — the
  compiled file, written beside its destination and renamed over it. Records in
  folded-tag order with a `u32` index over them; `descriptionOf` is a binary
  search and the only question the file answers. Where two entries name one tag
  **the first source named wins**, and within one source the first entry does.
- `refreshEcholist` / `echolistNeedsCompiling` (`echolist_compiler`) — the two
  entry points `main.cpp` uses.
- `EcholistAreaSource` (`echolist_area_source`) — the descriptions over the area
  list, as an `IAreaConfigSource` wrapping whatever the areas came from. The
  `ManualAreaSource` shape exactly, and it is outside the core for the reason
  [Layering](#layering) gives, so `main.cpp` and not `makeAreaSource()` is where
  the wrapping happens. The compiled file is opened afresh per `loadAreas()` —
  startup and each Ctrl-R — and **one that will not open leaves every
  description exactly as it was**.

**`arealist_description_priority`** is `area` or `echolist`, `area` by default.
**Only a non-empty description counts on either side**: the preferred one steps
aside for the other where it is empty, so an echo the tosser config says nothing
about is described by the echolist whichever way round the setting stands. That
is the whole of the rule and it lives in one `if` in `loadAreas()`. Where neither
side had anything to say the description stays empty here, and what the area
list's column draws in its place is `arealist_description_default`'s — a display
setting, read where the row is laid out and not in the area itself.

**The format is `echolist_format.hpp`, and its layout is documented there.**
`AMBERECH`, version 1, little-endian, and `format::kVersion` goes up on the same
terms the nodelist's does.

## The keyboard

`src/ui/keys.*` holds every binding AmberEdit has. A screen never compares a
keystroke against a key of its own: it asks `state.keys.is(event, command)`, and
what that answers is either AmberEdit's own layout or the file a `keys` line
named.

- **`KeyCommand` is the whole of what can be bound**, and `kCommands` in
  `keys.cpp` is the one place a command's name, its screen and its default keys
  are stated. Adding a command means adding a row there and asking for it in the
  screen — nothing else keeps a second list.
- **Only what runs a command is bindable.** Moving about — the arrows, PgUp and
  PgDn, Home and End, Space, Enter, Esc, Backspace, Tab — and every key inside a
  dialog stay where they are, so that no layout can leave a screen with no way
  out of it. `isReservedKey()` refuses those spellings outright rather than
  quietly dropping the line.
- **A file is the layout entire.** A command it does not name has no key, which
  is why `amberkeys.cfg.example` is the defaults written out: it is the thing to
  copy. A test parses that file and compares it against `KeyMap::defaults()`
  command by command, so the two cannot drift.
- **`KeyScreen` is what lets one key mean two things.** `F2` is Change in the
  reader and Save in the editor; two commands of *one* screen may not share a
  key, and `app.quit` is answered before every screen and so shares with nothing.
- **The layout is read in `main.cpp`**, before the terminal is taken over, so a
  file that cannot be read is reported like any other startup failure rather than
  falling back on defaults the user did not ask for. `config::AppConfig` carries
  the path and nothing else: a layout is about keystrokes and screens, and the
  config layer knows nothing of either.
- **A chord the layout does not bind is never typed.** Both halves of the
  editor swallow one — `headerKey()` and `textKey()` each end on
  `event.ctrl() || event.alt()` — since on a terminal reporting modified keys a
  chord arrives as the letter with a flag on it, and falling through to the
  insert would put that letter in the message.
- **Two things follow the layout rather than a key.** `Terminal` claims
  `ESC`+letter only for `KeyMap::altLetters()`, and the boxes that close on the
  key that opened them — Info, the nodelist — ask the layout what that key is.
- **A bare letter bound in the area list stops being a letter the quick search
  can be typed with**: the commands are answered before `searchInput()`. By
  default only `/` is taken there.
- **The hint bar reads the layout rather than naming keys of its own**
  (`ui/hint_bar.*`). Which commands each screen offers is a table there; the key
  in front of each is `KeyMap::preferredKey()` — a bare key before a chord,
  Ctrl before Alt, a chord before a function key — and a command the layout
  leaves unbound is left out of the row. `runApp()` takes the row off
  `state.height` before the screens lay themselves out, so no screen knows the
  bar is under it, and the row is taken whether or not there is anything to put
  in it: the message list has no commands, and a row that came and went between
  screens would move everything else on them. What is left of the row beside the
  hints is a rule in `separator` — the same rule that closes a screen's
  headings, closing the interface at the other end — and a screen with no hints
  leaves it whole. It is drawn after the modals in `document()`, so it says what
  the screen behind them does.
- **A hint is clicked by pressing its own key.** `hint_bar::clicked()` shows the
  press (`Pressed::Hint`, the index) and hands back
  `KeyMap::preferredKey()` — the very key the hint is written under — which
  `runApp()` gives to the screen in the click's place. Nothing reaches into a
  screen's commands: the row says which key runs a command, and a click that did
  anything else would make the row a lie. Where each hint landed comes off
  `render()` in `AppState::hintSpots`, as every other clickable thing does, and
  the two spaces between hints belong to the row so that a click lands on a hint
  or on nothing.

## Reference material in the tree

- `amberedit.cfg.example` — every setting, what it takes and what it defaults to.
- `amberkeys.cfg.example` — the default keyboard layout, written out as a `keys`
  file; a test keeps it equal to `KeyMap::defaults()`.
  User documentation and a test fixture both.
- `specs/` — format specifications: `Squish.txt`, `JAM.txt`, `fts-0001.016` (the
  base message and kludge format), and the ones a written message has to satisfy:
  `fts-0009.001` (MSGID/REPLY), `fts-4008.002` (TZUTC), `fts-5003.001` (CHRS) and
  `fsc-0004.001` (INTL).
- `TEMPLATE.md` — the GoldED message template format, which `app/msg_template`
  implements.
- `themes/` — `default.cfg` is the built-in palette written out,
  `ged_classic.cfg` a sixteen-color DOS one.
- `testdata/tossers/areas`, `areas.bbs`, `squish.cfg` — real tosser configs,
  which double as the parser test fixtures. Do not edit them to make a test pass.
- `testdata/nodelist/Z2DAILY.225` — a real day's Z2DAILY, 1227 nodes, ending in
  the `^Z` the real ones carry. `testdata/nodelist/Z2PNT.Z19` — a real zipped
  zone 2 pointlist, 2607 points, also the zip reader's fixture. Do not edit them
  to make a test pass.
- `testdata/echolist/echo50.lst` — a real R50 echolist in CP866, 106 echoes,
  with the comment blocks and the `Hold` statuses the real ones carry.
  `testdata/echolist/elst2601.zip` — a real ELIST distribution: three `.na` lists,
  and beside them the reports, the readme and the two further archives that are
  there to be left alone. Do not edit them to make a test pass.
- `testdata/msgbase/localnet.*` — the Squish test base described above.
- `testdata/msgbase/charsets.*` — a small Squish base for the charset tests: the
  word "Привет" in KOI8-R and in CP866, each with the CHRS kludge that says so,
  and a third copy with no kludge. Everything in it is addressed 2:382/9999. It
  is a binary fixture; to change it, write the messages again through
  `FtnMsgBase::write()` rather than editing the bytes.

## Current scope

Implemented: reading, writing and changing messages in Squish, JAM and Fido
`*.msg`; creating an empty base for an area that has none; all three tosser
config formats; charset detection and conversion in both directions, per area
where an area group says so; four screens (area list → message list → message
reader → compose) with a screen stack; message templates and quoting; the info
report behind `reader.info`; reading a file into a message being written, as text or
uuencoded, and writing one out again, as text or as the files it carries;
compiling nodelists and pointlists at startup when they change, and the nodelist
browser behind `reader.nodelist`; compiling echolists on the same terms, and the
descriptions they carry over the area list; twits, by name, address or subject, with the five `twit_mode`
answers to what becomes of one; finding a message in the area behind `reader.find`, folded
by the charset the message declares; the `CC:` and `XC:`/`XP:` lines a message
being written may carry, and the copies and crossposts they ask for; a keyboard
layout of one's own, from `keys`.

Deliberately out of scope until asked for:

- **Writing** the thread links: `IMsgBase::thread()` reads what a base holds —
  Squish's `replies[]`, JAM's Reply1st/ReplyNext chain, the one link FTS-0001
  gives Fido `*.msg` — and the reader walks them with `-`/`+`, but nothing fills
  them in for a message AmberEdit writes. That needs `replyto` on the new message
  and its UID added to the answered one. `IMsgBase::replace()` is the machinery
  for it, but it deliberately *keeps* those links rather than taking them from a
  draft, so a caller that wants to write one has to widen it first.
- Netmail routing, packing and unpacking bundles, anything involving sockets.
