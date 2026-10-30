#include "ui/header_format.hpp"

#include <algorithm>
#include <cstddef>

#include "i18n/i18n.hpp"
#include "ui/text_layout.hpp"

namespace amberedit::ui::header_format {

using config::HeaderFieldKind;
using config::HeaderLabel;

namespace {

bool isTyped(HeaderFieldKind kind) {
    return kind != HeaderFieldKind::Label && kind != HeaderFieldKind::Literal;
}

bool isStamp(HeaderFieldKind kind) {
    return kind == HeaderFieldKind::Written || kind == HeaderFieldKind::Arrived;
}

Line layoutLine(const config::HeaderLine& pieces, int width, const Measure& measure,
                const Cap& cap) {
    Line cells;
    cells.reserve(pieces.size());
    for (const auto& piece : pieces) {
        Cell cell;
        cell.kind = piece.kind;
        cell.label = piece.label;
        cell.text = piece.text;
        cells.push_back(std::move(cell));
    }

    // The fixed widths first, and with them a count of what is left to share.
    int fixed = 0;
    int stamps = 0;
    int flexible = 0;
    for (size_t at = 0; at < pieces.size(); ++at) {
        const auto& piece = pieces[at];
        if (piece.kind == HeaderFieldKind::Literal) {
            cells[at].width = displayWidth(piece.text);
        } else if (piece.kind == HeaderFieldKind::Label) {
            cells[at].width =
                piece.width == config::kAutoWidth ? labelWidth() : piece.width;
        } else if (piece.width == config::kAutoWidth && isStamp(piece.kind)) {
            ++stamps;
            continue;
        } else if (piece.width == 0) {
            ++flexible;
            continue;
        } else {
            cells[at].width = std::max(0, piece.width);
        }
        fixed += cells[at].width;
    }

    // The stamps' own pass, before the shares: what they do not use is worth
    // more to the fields written `0` than columns of blank after a date would
    // be. Each is held to an even share, so that neither crowds the other out.
    int spare = std::max(0, width - fixed);
    if (stamps > 0) {
        const int each = spare / stamps;
        for (size_t at = 0; at < pieces.size(); ++at) {
            const auto& piece = pieces[at];
            if (!isStamp(piece.kind) || piece.width != config::kAutoWidth) continue;
            cells[at].width = std::clamp(measure ? measure(piece) : 0, 0, each);
            spare -= cells[at].width;
        }
    }

    if (flexible > 0) {
        const int each = spare / flexible;
        int odd = spare % flexible;
        for (size_t at = 0; at < pieces.size(); ++at) {
            if (!isTyped(pieces[at].kind) || pieces[at].width != 0) continue;
            int share = each + (odd > 0 ? 1 : 0);
            if (odd > 0) --odd;
            // Only where nothing typed stands after it: a field narrowed in
            // the middle of a line would carry everything right of it out of
            // the columns it holds in the reader.
            const bool last = std::none_of(
                pieces.begin() + static_cast<std::ptrdiff_t>(at) + 1, pieces.end(),
                [](const config::HeaderField& next) { return isTyped(next.kind); });
            if (last && cap) {
                const int most = cap(pieces[at].kind);
                if (most > 0) share = std::min(share, most);
            }
            cells[at].width = share;
        }
    }

    // What runs past the right edge is cut there, a cell at a time, rather than
    // left for the row to squeeze: squeezed, a label loses letters from the
    // middle of the line while the field at the end keeps all of its own.
    int x = 0;
    for (auto& cell : cells) {
        cell.x = x;
        cell.width = std::clamp(width - x, 0, cell.width);
        x += cell.width;
    }
    return cells;
}

}  // namespace

const char* labelText(HeaderLabel label) {
    switch (label) {
        case HeaderLabel::From: return C_("header block", "From");
        case HeaderLabel::To: return C_("header block", "To");
        case HeaderLabel::Subject: return C_("header block", "Subj");
        case HeaderLabel::Date: return C_("header block", "Date");
        case HeaderLabel::Received: return C_("header block", "Recd");
    }
    return "";
}

int labelWidth() {
    // Measured once. The catalog is loaded before anything is drawn and does not
    // change under a running program, and this is asked for every line of every
    // frame.
    static const int width = [] {
        int widest = 4;
        for (const HeaderLabel label :
             {HeaderLabel::From, HeaderLabel::To, HeaderLabel::Subject, HeaderLabel::Date,
              HeaderLabel::Received}) {
            widest = std::max(widest, displayWidth(labelText(label)));
        }
        return widest;
    }();
    return width;
}

Layout layout(const config::HeaderFormat& format, int width, const Measure& measure,
              const Cap& cap) {
    Layout lines;
    lines.reserve(format.size());
    for (const auto& line : format)
        lines.push_back(layoutLine(line, width, measure, cap));
    return lines;
}

std::optional<int> locationColumn(const Layout& layout) {
    for (const auto& line : layout) {
        const bool holdsSender = std::any_of(
            line.begin(), line.end(),
            [](const Cell& cell) { return cell.kind == HeaderFieldKind::FromName; });
        if (!holdsSender) continue;
        int fields = 0;
        for (const auto& cell : line) {
            if (!isTyped(cell.kind)) continue;
            if (++fields == 2) return cell.x;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace amberedit::ui::header_format
