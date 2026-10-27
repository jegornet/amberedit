#include "app/clipboard.hpp"

#include "config/text_util.hpp"
#include "encoding/iconv_recoder.hpp"
#include "i18n/i18n.hpp"
#include "sys/clipboard.hpp"
#include "sys/process.hpp"

namespace amberedit::app {
namespace {

/// What a stage short of `Started` is said as, naming the program.
tl::unexpected<ErrorPtr> programFailure(const std::string& program,
                                        sys::CommandStage stage,
                                        const std::string& reason) {
    if (stage == sys::CommandStage::NotWaited) {
        return failure(i18n::format(_("cannot wait for {0}: {1}"), {program, reason}));
    }
    return failure(i18n::format(_("cannot run {0}: {1}"), {program, reason}));
}

}  // namespace

tl::expected<void, ErrorPtr> copyThroughProgram(const std::vector<std::string>& command,
                                                const std::string& text) {
    if (command.empty()) return {};
    const sys::CommandResult ran = sys::feedCommand(command, text);
    if (ran.stage == sys::CommandStage::Started) return {};
    return programFailure(command.front(), ran.stage, ran.reason);
}

tl::expected<std::string, ErrorPtr> pasteThroughProgram(
    const std::vector<std::string>& command) {
    if (command.empty()) return std::string();
    sys::CommandOutput ran = sys::readFromCommand(command);
    if (ran.stage == sys::CommandStage::Started) return std::move(ran.output);
    return programFailure(command.front(), ran.stage, ran.reason);
}

tl::expected<void, ErrorPtr> copyToSystem(const std::string& text) {
    const sys::ClipboardResult set = sys::setSystemClipboard(text);
    if (set.reason.empty()) return {};
    return failure(i18n::format(_("cannot copy to the clipboard: {0}"), {set.reason}));
}

tl::expected<std::string, ErrorPtr> pasteFromSystem() {
    sys::ClipboardResult got = sys::getSystemClipboard();
    if (got.reason.empty()) return std::move(got.text);
    return failure(i18n::format(_("cannot paste from the clipboard: {0}"), {got.reason}));
}

std::vector<std::string> pastedLines(std::string_view text) {
    // Through iconv only where it has to be: valid UTF-8 comes back as it
    // stands, and broken bytes come back as U+FFFD rather than as bytes the
    // editor would step through a character at a time and cut in half.
    std::string whole(text);
    if (!encoding::isValidUtf8(whole)) {
        encoding::IconvRecoder recoder;
        whole = recoder.toUtf8(whole, "UTF-8");
    }
    std::vector<std::string> lines = config::text::splitLines(whole);
    // A break closing the text is a line break pasted like any other: a line
    // copied whole goes in as a line, the text after the cursor carried down
    // under it, rather than run into it.
    if (!whole.empty() && whole.back() == '\n') lines.emplace_back();
    for (std::string& line : lines) line = config::text::messageLine(line);
    return lines;
}

}  // namespace amberedit::app
