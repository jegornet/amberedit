#include "ui/ansi_canvas.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "ui/term/utf8.hpp"

namespace amberedit::ui::ansi {
namespace {

constexpr char kEsc = '\x1b';
constexpr char kBel = '\x07';

/// The color a terminal is in before a message says anything: light grey on
/// black, the DOS attribute 0x07. Only ever needed where a code asks for the
/// *other* half of what is in force — an inversion has to have two concrete
/// colors to exchange — so everywhere else the canvas keeps -1, which is
/// `bbs::Color`'s "whatever the theme draws a message in" and lets a picture
/// that never set a background sit on the reader's own.
constexpr int kDefaultFg = 7;
constexpr int kDefaultBg = 0;

bool isParamByte(unsigned char c) { return c >= 0x30 && c <= 0x3F; }
bool isIntermediateByte(unsigned char c) { return c >= 0x20 && c <= 0x2F; }
bool isFinalByte(unsigned char c) { return c >= 0x40 && c <= 0x7E; }

/// The characters ECMA-48 lets follow an ESC on its own — `ESC M`, `ESC \` and
/// the rest. `[` is left out because it opens a CSI and `]` because it opens an
/// OSC, both of which run on past their second byte.
bool isSingleCharCommand(unsigned char c) {
    return (c >= 0x40 && c <= 0x5A) || (c >= 0x5C && c <= 0x5F);
}

/// The parameters of a CSI, in order, with an omitted one standing as 0 — which
/// is how ECMA-48 has it and why every mover below reads 0 as "no number given"
/// and uses its own default. `ESC[m` and `ESC[0m` are therefore the same thing,
/// as they are on a terminal.
std::vector<int> parameters(std::string_view params) {
    std::vector<int> out;
    int value = 0;
    bool any = false;
    for (const char c : params) {
        if (c >= '0' && c <= '9') {
            // Clamped as it is read: a message may write ESC[99999999999B, and
            // the number has to stay a number even though nothing will move
            // that far.
            value = std::min(value * 10 + (c - '0'), kMaxRows);
            any = true;
            continue;
        }
        if (c == ';') {
            out.push_back(value);
            value = 0;
            any = false;
            continue;
        }
        // A private-parameter byte (`?`, `<`, `=`, `>`): the sequence is not one
        // we act on, and its numbers are not ours to read either.
        return {};
    }
    if (any || !out.empty()) out.push_back(value);
    return out;
}

/// One cell of the canvas: what is drawn in it and in what colors.
struct Cell {
    /// The glyph, as UTF-8, or empty for a cell nothing was drawn in. A cell
    /// that a double-width glyph reaches into from the left is empty too and is
    /// marked `covered`, so that it takes a column of the canvas without
    /// putting a character of its own into the row.
    std::string glyph;
    bbs::Color color;
    bool covered{false};

    /// Whether the cell would draw nothing at all — no glyph and no color of
    /// its own — which is what makes it droppable at the end of a row. A blank
    /// carrying a background is not one of these: the background is what the
    /// picture drew there.
    [[nodiscard]] bool empty() const { return glyph.empty() && color.plain(); }
};

/// The terminal the message is replayed on: a grid that grows under the cursor,
/// the pen, and the position the two of them are at.
class Canvas {
public:
    void feed(std::string_view stream);
    [[nodiscard]] std::vector<bbs::CodedLine> lines(int columns) const;

private:
    std::vector<std::vector<Cell>> rows_;
    int row_{0};
    int col_{0};
    int savedRow_{0};
    int savedCol_{0};

    // The pen. `fg_` and `bg_` are -1 for the default — the theme's — or 0-7 as
    // the SGR color codes number them, which is the terminal's own order and so
    // needs no table to translate it, unlike the DOS order the pipe codes count
    // in.
    int fg_{-1};
    int bg_{-1};
    bool bright_{false};
    bool inverse_{false};
    bool hidden_{false};

    [[nodiscard]] bbs::Color pen() const;
    void reset();
    void applySgr(const std::vector<int>& params);
    void applyCsi(char final, const std::vector<int>& params);
    void eraseDisplay(int mode);
    void eraseLine(int mode);
    void eraseRow(int row, int from, int to);
    Cell& at(int row, int col);
    void put(std::string glyph, int width);
    void newline();
    void down(int n);
};

bbs::Color Canvas::pen() const {
    int fg = fg_;
    int bg = bg_;
    // Bright is the foreground's, and it is applied before the exchange below:
    // on the adapter these codes were written for, intensity was a bit of the
    // foreground nibble and an inversion swapped the nibbles whole.
    if (bright_) fg = (fg < 0 ? kDefaultFg : fg) | 8;
    if (inverse_) {
        // Both halves have to be concrete to be exchanged. A picture that
        // inverted without having named a color meant black on light grey, and
        // handing back two defaults would have shown no inversion at all.
        const int wasFg = fg < 0 ? kDefaultFg : fg;
        const int wasBg = bg < 0 ? kDefaultBg : bg;
        fg = wasBg;
        bg = wasFg;
    }
    return bbs::Color{fg, bg};
}

void Canvas::reset() {
    fg_ = -1;
    bg_ = -1;
    bright_ = false;
    inverse_ = false;
    hidden_ = false;
}

void Canvas::applySgr(const std::vector<int>& params) {
    if (params.empty()) {
        reset();
        return;
    }
    for (const int p : params) {
        switch (p) {
            case 0: reset(); break;
            case 1: bright_ = true; break;
            case 7: inverse_ = true; break;
            case 8: hidden_ = true; break;
            case 21: bright_ = false; break;
            case 27: inverse_ = false; break;
            case 28: hidden_ = false; break;
            case 39: fg_ = -1; break;
            case 49: bg_ = -1; break;
            default:
                if (p >= 30 && p <= 37) fg_ = p - 30;
                if (p >= 40 && p <= 47) bg_ = p - 40;
                // Everything else is passed over rather than drawn — blinking
                // above all, which is what 5 and 25 ask for. A reader that
                // flashed at the user would be a reader nobody could read, and
                // there is nothing else a message needs the attribute for.
                break;
        }
    }
}

Cell& Canvas::at(int row, int col) {
    if (static_cast<int>(rows_.size()) <= row) rows_.resize(static_cast<size_t>(row) + 1);
    auto& cells = rows_[static_cast<size_t>(row)];
    if (static_cast<int>(cells.size()) <= col) cells.resize(static_cast<size_t>(col) + 1);
    return cells[static_cast<size_t>(col)];
}

void Canvas::down(int n) {
    row_ = std::min(row_ + n, kMaxRows - 1);
}

void Canvas::newline() {
    // A line break in the message is the CR and the LF the terminal saw: the
    // art is written as chunks that step back up with an ESC[A afterwards, and
    // a break that only moved down would leave every one of them a column out.
    col_ = 0;
    down(1);
}

void Canvas::put(std::string glyph, int width) {
    // A combining mark belongs to the glyph in front of it and takes no column
    // of its own. It has nowhere to go at the start of a row, and is dropped
    // there rather than given a cell it would be drawn alone in.
    if (width <= 0) {
        if (col_ == 0) return;
        at(row_, col_ - 1).glyph += glyph;
        return;
    }
    // What is concealed is written as the blank it is drawn as, rather than
    // kept and hidden at drawing time: 28 turns the attribute off for what
    // comes after it and says nothing about what has already been put down.
    if (hidden_) glyph.clear();

    Cell& cell = at(row_, col_);
    cell.glyph = std::move(glyph);
    cell.color = pen();
    cell.covered = false;
    for (int i = 1; i < width && col_ + i < kColumns; ++i) {
        Cell& under = at(row_, col_ + i);
        under.glyph.clear();
        under.color = cell.color;
        under.covered = true;
    }

    col_ += width;
    // The wrap is immediate and not deferred: the art counts on the glyph after
    // the one in column 80 standing at the start of the next row, and a
    // pending-wrap state that the next cursor move cancelled would put it back
    // on top of the border it just drew.
    if (col_ >= kColumns) {
        col_ = 0;
        down(1);
    }
}

void Canvas::eraseRow(int row, int from, int to) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    auto& cells = rows_[static_cast<size_t>(row)];
    const int last = std::min(to, static_cast<int>(cells.size()) - 1);
    // The background in force is what an erase leaves behind — the rest of the
    // line after ESC[41m;ESC[K is red, not the theme's. Only cells the picture
    // has already reached are cleared: the canvas has no height of its own to
    // paint out to, and growing it here would turn every ESC[2J into rows of
    // nothing.
    const bbs::Color blank{-1, pen().bg};
    for (int i = std::max(0, from); i <= last; ++i) {
        cells[static_cast<size_t>(i)] = Cell{std::string{}, blank, false};
    }
}

void Canvas::eraseDisplay(int mode) {
    const int last = static_cast<int>(rows_.size()) - 1;
    if (mode == 0) {
        eraseRow(row_, col_, kColumns);
        for (int r = row_ + 1; r <= last; ++r) eraseRow(r, 0, kColumns);
    } else if (mode == 1) {
        for (int r = 0; r < row_; ++r) eraseRow(r, 0, kColumns);
        eraseRow(row_, 0, col_);
    } else if (mode == 2) {
        for (int r = 0; r <= last; ++r) eraseRow(r, 0, kColumns);
    }
    // The cursor stays where it is, as ECMA-48 has it. A picture that wants to
    // draw from the corner afterwards says so, and every one of them does.
}

void Canvas::eraseLine(int mode) {
    if (mode == 0) eraseRow(row_, col_, kColumns);
    else if (mode == 1) eraseRow(row_, 0, col_);
    else if (mode == 2) eraseRow(row_, 0, kColumns);
}

void Canvas::applyCsi(char final, const std::vector<int>& params) {
    // Each parameter as the sequence's own default where the message left it
    // out, which for every mover here is 1 and for the erasers is 0.
    const auto arg = [&params](size_t index, int fallback) {
        if (index >= params.size() || params[index] == 0) return fallback;
        return params[index];
    };
    switch (final) {
        case 'A': row_ = std::max(0, row_ - arg(0, 1)); break;
        case 'B': down(arg(0, 1)); break;
        case 'C': col_ = std::min(kColumns - 1, col_ + arg(0, 1)); break;
        case 'D': col_ = std::max(0, col_ - arg(0, 1)); break;
        case 'E':
            down(arg(0, 1));
            col_ = 0;
            break;
        case 'F':
            row_ = std::max(0, row_ - arg(0, 1));
            col_ = 0;
            break;
        case 'G': col_ = std::clamp(arg(0, 1) - 1, 0, kColumns - 1); break;
        case 'H':
        case 'f':
            row_ = std::min(std::max(0, arg(0, 1) - 1), kMaxRows - 1);
            col_ = std::clamp(arg(1, 1) - 1, 0, kColumns - 1);
            break;
        case 'J': eraseDisplay(arg(0, 0)); break;
        case 'K': eraseLine(arg(0, 0)); break;
        case 'm': applySgr(params); break;
        // The other spelling of the save and restore that ESC 7 and ESC 8 are,
        // and the one BBS art is actually written with — a message may use
        // either, and both mean the position and not the pen. It is the same
        // idiom the `ESC[A` files use: save, let the newline fall, restore, and
        // carry the row on where it left off.
        case 's': savedRow_ = row_; savedCol_ = col_; break;
        case 'u': row_ = savedRow_; col_ = savedCol_; break;
        // Anything else is a sequence this reader has no answer for. It is
        // stepped over rather than drawn: the bytes of a code nobody acted on
        // are still not text.
        default: break;
    }
}

void Canvas::feed(std::string_view stream) {
    size_t i = 0;
    while (i < stream.size()) {
        const char c = stream[i];
        if (c == kEsc) {
            const size_t length = escapeLength(stream, i);
            if (length == 0) {
                // An ESC that opens nothing. Only the ESC goes: what follows it
                // was never part of a sequence and is the message's own text.
                ++i;
                continue;
            }
            if (stream[i + 1] == '[') {
                size_t at = i + 2;
                const size_t begin = at;
                while (at < stream.size() &&
                       isParamByte(static_cast<unsigned char>(stream[at]))) {
                    ++at;
                }
                const std::string_view params = stream.substr(begin, at - begin);
                applyCsi(stream[i + length - 1], parameters(params));
            } else if (stream[i + 1] == '7') {
                savedRow_ = row_;
                savedCol_ = col_;
            } else if (stream[i + 1] == '8') {
                row_ = savedRow_;
                col_ = savedCol_;
            }
            i += length;
            continue;
        }
        if (c == '\n') {
            newline();
            ++i;
            continue;
        }
        if (c == '\r') {
            col_ = 0;
            ++i;
            continue;
        }
        // Every other control character — the ^Z many an ANSI file still ends
        // with among them — is neither a code nor a glyph and leaves no mark.
        if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f') {
            ++i;
            continue;
        }

        size_t next = i;
        const char32_t code = term::decodeUtf8(stream, next);
        put(std::string(stream.substr(i, next - i)), term::codepointWidth(code));
        i = next;
    }
}

std::vector<bbs::CodedLine> Canvas::lines(int columns) const {
    const int cut = std::clamp(columns, 1, kColumns);
    std::vector<bbs::CodedLine> out;
    out.reserve(rows_.size());

    for (const auto& cells : rows_) {
        int end = std::min(cut, static_cast<int>(cells.size()));
        while (end > 0 && cells[static_cast<size_t>(end - 1)].empty()) --end;

        bbs::CodedLine line;
        bbs::Color inForce;
        for (int i = 0; i < end; ++i) {
            const Cell& cell = cells[static_cast<size_t>(i)];
            if (cell.color != inForce) {
                inForce = cell.color;
                line.runs.push_back({line.text.size(), inForce});
            }
            if (cell.covered) continue;
            line.text += cell.glyph.empty() ? " " : cell.glyph;
        }
        out.push_back(std::move(line));
    }

    // The rows the cursor passed over on its way out of the picture. A message
    // that ended with ESC[B has no more art in it, and blank rows under one are
    // blank rows to scroll through.
    while (!out.empty() && out.back().text.empty() && out.back().runs.empty()) {
        out.pop_back();
    }
    return out;
}

}  // namespace

size_t escapeLength(std::string_view text, size_t at) {
    if (at >= text.size() || text[at] != kEsc) return 0;
    if (at + 1 >= text.size()) return 0;
    const unsigned char second = static_cast<unsigned char>(text[at + 1]);

    if (second == '[') {
        size_t i = at + 2;
        while (i < text.size() && isParamByte(static_cast<unsigned char>(text[i]))) ++i;
        while (i < text.size() && isIntermediateByte(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size() || !isFinalByte(static_cast<unsigned char>(text[i]))) return 0;
        return i + 1 - at;
    }
    if (second == ']') {
        // An OSC runs to a BEL or to the ST that ESC \ spells. Nothing in a
        // message is addressed to the terminal's title bar or its palette, so
        // this is here to be swallowed whole and not to be acted on — but it has
        // to be swallowed, or its payload would be drawn as text.
        for (size_t i = at + 2; i < text.size(); ++i) {
            if (text[i] == kBel) return i + 1 - at;
            if (text[i] == kEsc) {
                if (i + 1 < text.size() && text[i + 1] == '\\') return i + 2 - at;
                return 0;
            }
        }
        return 0;
    }
    // The DEC pair, which is not a CSI: ESC 7 and ESC 8 save and restore where
    // the cursor stands.
    if (second == '7' || second == '8') return 2;
    if (isSingleCharCommand(second)) return 2;
    return 0;
}

bool containsCodes(std::string_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != kEsc) continue;
        if (escapeLength(text, i) > 0) return true;
    }
    return false;
}

std::vector<bbs::CodedLine> render(std::string_view stream, int columns) {
    Canvas canvas;
    canvas.feed(stream);
    return canvas.lines(columns);
}

}  // namespace amberedit::ui::ansi
