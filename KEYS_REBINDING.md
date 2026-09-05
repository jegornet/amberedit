# Rebinding the keys

The standard layout is in [KEYS.md](KEYS.md), and `F1` shows it inside the
program. This is how to write one of your own.

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

A command is written as where it is answered, a dot, and the command itself —
`reader.list`, `app.quit`. What stands in front of the dot is what lets one key
mean two things: `F2` is `reader.change` and `compose.save` at once, the reader
and the editor never being the same screen.

| Written | Answered on | Commands |
|---|---|---|
| `app.` | every screen | `quit`, `help` |
| `arealist.` | the area list | `next_unread`, `toggle_unread`, `rescan` |
| `msglist.` | the message list | `mark_toggle` |
| `reader.` | the reader | `reply`, `reply_elsewhere`, `comment_reply`, `new`, `forward`, `change`, `delete`, `export`, `find`, `list`, `info`, `nodelist`, `kludges`, `scrollbar`, `thread_up`, `thread_down`, `mark_toggle`, `mark_menu`, `shell` |
| `compose.` | the editor | `save`, `attributes`, `import`, `header_back`, `delete_line`, `restore_line`, `delete_quote`, `delete_word`, `word_left`, `word_right`, `line_start`, `line_end` |

`app.quit` is answered ahead of every dialog as well: no box is worth being the
one thing between you and the way out. Everything else waits until the box in
front of you has been closed — `app.help` included, a dialog answering for its
own keys.

The area list, the reader and the editor each answer `extern_util0` through
`extern_util9` besides, for the programs `amberedit.cfg` names. The digit is the
program and the screen in front of the dot is only where the key is pressed, so
`Alt-F1 reader.extern_util0` and `Alt-F1 compose.extern_util0` run the same one
from two screens.
