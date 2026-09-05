# The keys

The standard layout, screen by screen: what AmberEdit answers to when no `keys`
file has been named. Every key here that runs a command can be moved — see
[KEYS_REBINDING.md](KEYS_REBINDING.md).

**Ctrl-Q** quits from anywhere, and **F1** opens this same list inside the
program, for whichever screen is up. That box is the layout as it stands rather
than this page: a key that has been rebound is the key it shows, and a command
left unbound is not in it at all.

## Area list

| Key | What it does                                                     |
|---|------------------------------------------------------------------|
| `↑` `↓` `PgUp` `PgDn` `Space`, `Home` `End` | move                                                             |
| `Enter`, `→`, click | open the area under the cursor                                   |
| any letter | search by area tag; `Backspace` edits the query, `Esc` closes it |
| `/` | go to the next area with unread messages                         |
| `Ctrl-U` | show only the areas with unread messages, or all of them again |
| `Ctrl-R` | rescan the message bases                                   |
| `Esc` | quit                                                             |

## Message list

| Key | What it does |
|---|---|
| `↑` `↓` `PgUp` `PgDn` | move |
| `Home`, `End` | first, last |
| `Enter`, `→`, click | open the message |
| `t`, `Space` | mark the message under the cursor, or take the mark off it |
| any digit | go to a message by number: the title's `12/44` becomes a field standing in exactly those columns, `Enter` opens that message — or puts the cursor on its row with `msglist_goto_field_opens off` — `Backspace` edits it, `Esc` closes it |
| `Esc` `←` `Backspace` | back to the area list |

## Reader

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

## Editor

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
