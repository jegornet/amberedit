#include "support/error.hpp"

#include <string>

#include "i18n/i18n.hpp"

namespace amberedit {

std::string ConfigError::message() const {
    // `t.cfg:3: quote_margin must be between 20 and 255`, which is what every
    // config diagnostic has read as since there were any. A line of zero is a
    // complaint about the file rather than a line of it, and then there is no
    // number worth printing: `t.cfg: there is no area list`.
    // Not a translated message: the two parts are joined the way a compiler
    // joins them, and `what_` is the half that carries the words.
    if (line_ <= 0) return origin_ + ": " + what_;
    return origin_ + ":" + std::to_string(line_) + ": " + what_;
}

std::string MsgBaseError::message() const {
    // Word for word what each of these has said since there was a driver to say
    // it: the sentences are what the tests assert on and what a user has read
    // before, and the Kind beside them is the new half.
    switch (kind_) {
        case Kind::NoAreaOpen: return _("no area is open");
        case Kind::Passthrough:
            return i18n::format(_("area {0} is passthrough: there is no base on disk"),
                                {subject_});
        case Kind::Absent:
        case Kind::WrongFormat:
            return i18n::format(_("no {0} base at {1}"), {detail_, subject_});
        case Kind::UnknownType:
            return i18n::format(_("cannot determine the base type for {0}"), {subject_});
        case Kind::AlreadyExists:
            return i18n::format(_("there is already a base at {0}"), {subject_});
        case Kind::CannotMakeType:
            return i18n::format(_("cannot create a base of type {0}"), {subject_});
        case Kind::CannotOpen:
            return i18n::format(_("cannot open base {0}: {1}"), {subject_, detail_});
        case Kind::BaseBusy: return i18n::format(_("{0}: the base is busy"), {subject_});
        case Kind::MessageBusy:
            return i18n::format(_("{0}: the message is busy"), {subject_});
    }
    return subject_;
}

}  // namespace amberedit
