#include "support/error.hpp"

#include <string>

namespace amberedit {

std::string ConfigError::message() const {
    // `t.cfg:3: quote_margin must be between 20 and 255`, which is what every
    // config diagnostic has read as since there were any. A line of zero is a
    // complaint about the file rather than a line of it, and then there is no
    // number worth printing: `t.cfg: there is no area list`.
    if (line_ <= 0) return origin_ + ": " + what_;
    return origin_ + ":" + std::to_string(line_) + ": " + what_;
}

std::string MsgBaseError::message() const {
    // Word for word what each of these has said since there was a driver to say
    // it: the sentences are what the tests assert on and what a user has read
    // before, and the Kind beside them is the new half.
    switch (kind_) {
        case Kind::NoAreaOpen: return "no area is open";
        case Kind::Passthrough:
            return "area " + subject_ + " is passthrough: there is no base on disk";
        case Kind::Absent:
        case Kind::WrongFormat: return "no " + detail_ + " base at " + subject_;
        case Kind::UnknownType: return "cannot determine the base type for " + subject_;
        case Kind::AlreadyExists: return "there is already a base at " + subject_;
        case Kind::CannotMakeType: return "cannot create a base of type " + subject_;
        case Kind::CannotOpen: return "cannot open base " + subject_ + ": " + detail_;
        case Kind::BaseBusy: return subject_ + ": the base is busy";
        case Kind::MessageBusy: return subject_ + ": the message is busy";
    }
    return subject_;
}

}  // namespace amberedit
