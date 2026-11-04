# The keys

The standard layout, screen by screen: what AmberEdit answers to when no `keys`
file has been named. Every key here that runs a command can be moved — see
[KEYS_REBINDING.md](KEYS_REBINDING.md).

**Ctrl-Q** quits from anywhere, and **F1** opens this same list inside the
program, for whichever screen is up. That box is the layout as it stands rather
than this page: a key that has been rebound is the key it shows, and a command
left unbound is not in it at all.

**Ctrl-L** draws the whole screen again, for when another program writing to the
terminal has left it in pieces. It works everywhere but in the attributes box,
where it is Local.

**Esc breaks off a long run over the marked messages** — deleting, copying or
moving a whole area's worth — while the box counting them is on the screen.
What it had already done stands, and every message it never reached is still
marked, so the set is there to act on again. The one thing it does not stop is
the end of a move: messages already written into the other area are taken out of
this one whatever is pressed, since stopping there would leave the same message
standing in both.

## Area list

| Key | What it does                                                     |
|---|------------------------------------------------------------------|
| `↑` `↓` `PgUp` `PgDn` `Space`, `Home` `End` | move                                                             |
| `Enter`, `→`, click | open the area under the cursor                                   |
| any letter | search by area tag; `Backspace` edits the query, `Esc` closes it |
| `/` | go to the next area with unread messages                         |
| `Ctrl-U` | show only the areas with unread messages, or all of them again |
| `Ctrl-R` | rescan the message bases                                   |
| `Ctrl-T` | mark the area under the cursor, or take the mark off it, and step down to the next one (`mark_moves_down off` disables the latter) |
| `Alt-C` | catch up — mark the area read to its newest message; with areas marked, asks whether you mean those or this one |
| `Esc` | quit                                                             |

## Message list

| Key | What it does |
|---|---|
| `↑` `↓` `PgUp` `PgDn` | move |
| `Home`, `End` | first, last |
| `Enter`, `→`, click | open the message |
| `Ctrl-T`, `Space` | mark the message under the cursor, or take the mark off it, and step down to the next one (`mark_moves_down off` disables the latter) |
| `d`, `Del` | delete the message under the cursor, after asking; with messages marked, asks whether you mean those or this one |
| any digit | go to a message by number: the title's `12/44` becomes a field standing in exactly those columns, `Enter` opens that message — or puts the cursor on its row with `msglist_goto_field_opens off` — `Backspace` edits it, `Esc` closes it |
| `Esc` `←` `Backspace` | back to the area list |

## Reader

| Key | What it does                                                                 |
|---|------------------------------------------------------------------------------|
| `←` `→` | previous, next message — off either end leaves the area, or → goes on to the next unread area (`reader_edge`); with `reader_edge_confirm on` it asks first |
| click on the side of the text | the same two, from the pointer: the columns down either side of the message are ← and →; see `reader_side_taps`, `reader_side_tap_width`, `adaptive_ui_threshold`  |
| `↑` `↓` `PgUp` `PgDn` `Space` `Shift+Space` | scroll the message                                                           |
| `Home`, `End` | top, bottom                                                                  |
| `Ctrl-Home`, `Ctrl-End` | the first, the last message of the area |
| any digit | go to a message by number: the title's `12/44` becomes a field standing in exactly those columns, `Enter` goes there, `Backspace` edits it, `Esc` closes it |
| `q` / `F4` / `Alt-Q` | reply                                                              |
| `e` / `Ins` / `Alt-E` | write a new message                                                  |
| `n` / `F5` / `Alt-N` | reply into another area                                                      |
| `g` / `Alt-G` | reply, addressed to whoever the message was written to                 |
| `m` / `Alt-M` | forward, move or copy into another area — with messages marked, asks whether you mean those (copy/move) or this one |
| `c` / `F2` / `Alt-C` | change the message                                                           |
| `d` / `Del` / `Alt-D` | delete it (asks first) — with messages marked, asks whether you mean those or this one |
| `-` `+` / `=` | up and down the thread                                                       |
| `Ctrl-F` / `F6` | look for a message in the area                                               |
| `l` / `F9` / `Alt-L` | the list of messages                                                         |
| `k` / `Alt-K` | show the kludges — a reply then quotes them, a forward carries them          |
| `h` / `Alt-H` | read this message in another charset |
| `b` / `Alt-B` | toggle the scrollbar                                                         |
| `Ctrl-T` | mark the message, or take the mark off it                               |
| `s` / `Alt-S` | mark a run of messages at once: all, none, inverted, or everything before or after this one |
| `Ctrl-X` | run your own shell — leaving it comes straight back to the message      |
| `Ctrl-C` / `Ctrl-Ins` | copy the message to the clipboard (`clipboard`), header and all, as `w` writes it; kludges only where `k` shows them |
| `i` / `Alt-I` | technical info from the message base                                         |
| `w` / `F7` / `Alt-W` | export the message to a text file or decode UUE if any — with messages marked, asks whether you mean those or this one |
| `Ctrl-N` / `F10` | the nodelist                                                                 |
| `Space` | show a message blanked as a twit                                             |
| `Esc` `Backspace` | back to the area list                                                        |

## Editor

| Key | What it does                                                       |
|---|--------------------------------------------------------------------|
| `Tab` `Shift-Tab` | the ring: the header's fields and the attributes button as they are read — along a line, then down — and the text |
| `Enter` | the next field the same way, past the button, then down into the text |
| `↑` `↓` in the header | the field above or below as the block is drawn; off the bottom, the text |
| `←` `→` at the edge of a field | the field or button beside it on the same line |
| `Alt-H` | edit header                                                         |
| `Ctrl-S` / `F2` / `Alt-S` | save (asks first)                                         |
| `Esc` | drop the message (asks first)                                      |
| `Ctrl-F` | edit message attributes                                         |
| `Ctrl-O` / `F3` | open a file to insert as text or uuencoded             |
| `Ctrl-Y` / `Alt-D` | delete the line — or the button beside it, on the right-hand edge |
| `Ctrl-U` | put back the last line `Ctrl-Y` took, above the cursor              |
| `Ctrl-D` / `Alt-Z` | delete the quoted text after the cursor                   |
| `Ctrl-W` / `Alt-Backspace` / `Ctrl-Backspace` | delete the word before the cursor; `Ctrl-Backspace` only under the kitty keyboard protocol |
| `Alt-Del` / `Ctrl-Del` | delete the word after the cursor                           |
| `Alt-B` `Alt-F` / `Alt+←` `Alt+→` / `Ctrl+←` `Ctrl+→` | by words                   |
| `Ctrl-A` `Ctrl-E` | to the start of the line and to its end, as `Home` and `End`     |
| `Ctrl-Home` `Ctrl-End` | to the start of the text and to its end, from the header too |
| `Ctrl-V` / `Shift-Ins` | paste from the clipboard (`clipboard`) at the cursor; many terminals keep `Shift-Ins` for their own paste |

## Keys a terminal cannot send

Some chords leave the terminal as the same bytes as another key, and AmberEdit
reads them as that key. Measured in 19 Linux terminals and 5 on macOS; the list
with the bytes is `testdata/keys/limits.lst`.

- `Ctrl-H`, `Ctrl-I`, `Ctrl-J`, `Ctrl-M` and `Ctrl-Backspace` are `Backspace`,
  `Tab` and `Enter`, except under the kitty keyboard protocol in kitty, foot,
  Ghostty, Alacritty, Contour, Konsole and WezTerm with `enable_kitty_keyboard`
  (`modifier_key_reporting`).
- `Shift+Space` is `Space` everywhere but WezTerm under `xterm`.
- In urxvt and PuTTY, Alt with an arrow, `Del` or a function key is `Esc` and
  then the key. Alt with a letter works where the layout binds that letter.
- WezTerm with `enable_kitty_keyboard` on sends `Del` as `Backspace` under the
  kitty protocol; `modifier_key_reporting xterm` avoids it.
- st sends `Ctrl-Home` as `Home` and `Alt-Del` as `Del`; PuTTY sends
  `Ctrl+←` `Ctrl+→` as the bare arrows and `Ctrl-Del` as `Backspace`.
- Many terminals keep `F1`, `F10`, `F11`, `Shift-Ins`, `Ctrl-Ins` or Alt
  with a menu letter for themselves, and AmberEdit never sees them.
- Inside tmux or screen the kitty keyboard protocol does not get through, so
  `Ctrl-Backspace` and `Ctrl-H` are `Backspace` there whatever the terminal
  outside. tmux with `set -s extended-keys on` passes `Shift+Space` on where
  `modifier_key_reporting` names `xterm`.
- On macOS, Option is not Alt until the terminal is told so (iTerm2: *Left
  Option key: Esc+*; Terminal.app: *Use Option as Meta key*; kitty:
  `macos_option_as_alt`; Ghostty: `macos-option-as-alt`; WezTerm:
  `send_composed_key_when_left_alt_is_pressed`).
  Until then Option with a letter types a character, Option-←/→ moves by words
  in iTerm2 and Terminal.app, and Option with a function key or an arrow is the
  bare key. A Mac keyboard's Help key is not Insert.
