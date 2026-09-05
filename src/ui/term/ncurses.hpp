#pragma once

/// The one place ncurses is included from.
///
/// Which header to ask for is not obvious and getting it wrong is quiet rather
/// than loud. On a system with both a wide and a narrow ncurses installed,
/// `<curses.h>` is the narrow one — and on macOS, `<ncurses.h>` resolves to the
/// ncurses 5.7 that Apple still ships, which has neither `cchar_t` nor the
/// extended color pairs, so the mistake only shows up as a compile error deep in
/// this directory. Distributions that keep the wide headers apart put them under
/// `ncursesw/`, which is unambiguous wherever it exists; CMake checks for it and
/// says so here.
///
/// Only `curses.h` is asked for. Nothing here uses terminfo — no `setupterm`,
/// no `tigetflag`, no `cur_term` — and `<term.h>` was being included for a
/// dependency the code does not have. Saying so matters beyond tidiness:
/// PDCurses, which the Windows build uses, has no terminfo at all.
#if defined(AMBEREDIT_NCURSESW_SUBDIR)
#include <ncursesw/curses.h>
#else
#include <curses.h>
#endif

// How many wide characters one cell can hold: a base character and whatever
// combines onto it. ncurses says five and has always spelled it in its header.
// PDCursesMod reads twenty, but only names the limit in releases after 4.5.4 —
// and 4.5.4 is what MSYS2 packages, so the number is repeated here where the
// header does not have it. It is the library's own cap either way: `setcchar`
// there stops at the twentieth character whatever this side asks for.
#if defined(__PDCURSESMOD__) && !defined(CCHARW_MAX)
#define CCHARW_MAX 20
#endif

// Both headers are C from before namespaces existed, and define a handful of
// very ordinary words as bare macros. `lines`, `columns` and `clear` collide
// with the sort of name this code is full of — `Screen::clear()` among them —
// and the error that results names neither the macro nor the header. They come
// straight back off, which is the whole reason every other file includes this
// one rather than ncurses directly.
//
// Taking a macro off does not take the function away: ncurses exports a real
// one under each of these names as well. Even so, the code below calls the
// explicit `w*` forms — `wrefresh(stdscr)` rather than `refresh()` — so that
// which of the two is being used never has to be worked out from context.
#undef lines
#undef columns
#undef tab
#undef buttons
#undef timeout
#undef erase
#undef clear
#undef move
#undef refresh
#undef border
#undef box
#undef scroll
#undef instr
