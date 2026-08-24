#pragma once

#include <memory>
#include <string>
#include <utility>

#include <tl/expected.hpp>

namespace amberedit {

/// Why a fallible operation could not do what was asked.
///
/// A closed set of subclasses, each holding what its own kind of failure knows
/// and nothing else: a config error knows a file and a line, a message-base
/// error knows a base and an offset in it. The sentence a person reads is built
/// by `message()` out of those fields rather than stored as text, which is what
/// lets one failure be both shown to somebody and asked about by the code above
/// it — the two things a bare string could not do at once.
///
/// **An error travels as `ErrorPtr` and is moved, never copied.** That is the
/// whole reason it is held behind a pointer: an answer carrying eight bytes costs
/// nothing to hand upwards, and the copy that a `std::string` error invited at
/// every frame of the way out will not compile.
///
/// **Nothing here is thrown.** `Error` is a value like any other and has no
/// relation to `std::exception`; the three places AmberEdit catches are catching
/// what the standard library throws underneath, not this.
class Error {
public:
    virtual ~Error() = default;

    Error(const Error&) = delete;
    Error& operator=(const Error&) = delete;

    /// The whole of what went wrong, as a person is to read it: already
    /// complete, naming the file, the line, the area. Built on demand, because
    /// the overwhelming majority of errors are read once at the top of the call
    /// stack and a great many are never read at all.
    [[nodiscard]] virtual std::string message() const = 0;

protected:
    Error() = default;
};

/// How an error is held and handed on. Move-only, eight bytes.
using ErrorPtr = std::unique_ptr<const Error>;

/// An error that is no more than its own sentence.
///
/// For the failures that carry nothing a caller could branch on — and, while the
/// tree moves over to the typed errors beside it, for the ones that have not
/// been given a class yet. `failure("…")` below builds one.
class PlainError final : public Error {
public:
    explicit PlainError(std::string message) : message_(std::move(message)) {}

    [[nodiscard]] std::string message() const override { return message_; }

private:
    std::string message_;
};

/// A line of a config, a theme, a keyboard layout or a template would not do.
///
/// The file and the line are held apart from what was wrong with them rather
/// than pasted in front of it, which is the whole point: `main()` prints the
/// sentence, and anything that wants to say *which* file to open — an editor, a
/// setup wizard, a future `--check` — can have the parts instead.
///
/// `CfgEntry::fail()` in `config/cfg_file.hpp` is what builds most of these, so
/// every diagnostic read through a config entry names its line without the
/// caller doing anything.
class ConfigError final : public Error {
public:
    ConfigError(std::string origin, int line, std::string what)
        : origin_(std::move(origin)), line_(line), what_(std::move(what)) {}

    /// The file the line came from, as the config named it.
    [[nodiscard]] const std::string& origin() const { return origin_; }
    /// Its number in that file, counting from one. Zero where the complaint is
    /// about the file as a whole rather than a line of it.
    [[nodiscard]] int line() const { return line_; }
    /// What was wrong, without the file and line in front of it.
    [[nodiscard]] const std::string& what() const { return what_; }

    [[nodiscard]] std::string message() const override;

private:
    std::string origin_;
    int line_;
    std::string what_;
};

/// A message base would not open, would not be made, or would not be written.
///
/// The `Kind` is the half a caller can act on, and it is the reason this class
/// exists rather than a sentence: `AreaManager::openArea` has to tell a base
/// that is not there — which it offers to create — from one that is there and
/// broken, and used to walk the file system a second time to find out.
///
/// `Absent` and `WrongFormat` read the same to a person, and deliberately: what
/// they say is that the base the config named is not where it said. They are
/// separate here because only the first is a base that creating one would
/// supply.
class MsgBaseError final : public Error {
public:
    enum class Kind {
        /// A driver was asked to read or write with no area open. A caller's
        /// mistake rather than a user's.
        NoAreaOpen,
        /// The area carries no base by design, so there is nothing to open.
        Passthrough,
        /// Nothing at all is at the path the config named.
        Absent,
        /// Something is there, but not the format that was asked for.
        WrongFormat,
        /// Nothing states the format and nothing on disk suggests one.
        UnknownType,
        /// Asked to create a base where one already is.
        AlreadyExists,
        /// A format in the enum that `makeDriver()` does not build.
        CannotMakeType,
        /// The driver refused: `detail` is its own words.
        CannotOpen,
        /// Another task holds the base, or the one message. `subject` is the
        /// lock's own complaint.
        BaseBusy,
        MessageBusy,
    };

    MsgBaseError(Kind kind, std::string subject, std::string detail = {})
        : kind_(kind), subject_(std::move(subject)), detail_(std::move(detail)) {}

    [[nodiscard]] Kind kind() const { return kind_; }
    /// The area tag, the base path or the format name — whichever the sentence
    /// for this `Kind` names.
    [[nodiscard]] const std::string& subject() const { return subject_; }

    [[nodiscard]] std::string message() const override;

private:
    Kind kind_;
    std::string subject_;
    std::string detail_;
};

/// The failure half of an answer, for `return failure("…")`. It converts to a
/// `tl::expected` of any T, which is what lets a helper that only ever fails —
/// and a deep return out of a long function — say so without naming the type
/// again.
///
/// This one builds a `PlainError`: an error that is no more than its sentence.
/// Where a caller above could want to know what kind of failure it was, reach
/// for `failure<SomeError>(…)` below and a class that holds the parts.
[[nodiscard]] inline tl::unexpected<ErrorPtr> failure(std::string message) {
    return tl::make_unexpected<ErrorPtr>(
        std::make_unique<const PlainError>(std::move(message)));
}

/// The same, for one of the typed errors: `failure<ConfigError>(origin, line, …)`.
template <typename E, typename... Args>
[[nodiscard]] tl::unexpected<ErrorPtr> failure(Args&&... args) {
    return tl::make_unexpected<ErrorPtr>(
        std::make_unique<const E>(std::forward<Args>(args)...));
}

}  // namespace amberedit
