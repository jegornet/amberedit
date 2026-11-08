#include "app/run_program.hpp"

#include <string_view>
#include <utility>

#include "config/text_util.hpp"
#include "encoding/iconv_recoder.hpp"
#include "i18n/i18n.hpp"
#include "sys/process.hpp"

namespace amberedit::app {

namespace {

tl::expected<void, ErrorPtr> answer(const std::string& program,
                                    const sys::CommandResult& result) {
    switch (result.stage) {
        case sys::CommandStage::Started: return {};
        case sys::CommandStage::NotStarted:
            return failure(
                i18n::format(_("cannot run {0}: {1}"), {program, result.reason}));
        case sys::CommandStage::NotWaited:
            return failure(
                i18n::format(_("cannot wait for {0}: {1}"), {program, result.reason}));
    }
    return {};
}

/// `line` with its escape sequences taken out: a CSI sequence to its final
/// byte, an OSC string to the BEL or the ST that ends it, and anything else
/// after an ESC as the one character it is. A sequence the line ends inside of
/// is taken out to the end.
std::string withoutEscapes(std::string_view line) {
    std::string kept;
    kept.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '\x1b') {
            kept += line[i];
            continue;
        }
        if (i + 1 >= line.size()) break;
        const char kind = line[i + 1];
        i += 1;
        if (kind == '[') {
            while (i + 1 < line.size()) {
                const auto byte = static_cast<unsigned char>(line[++i]);
                if (byte >= 0x40 && byte <= 0x7E) break;
            }
        } else if (kind == ']') {
            while (i + 1 < line.size()) {
                const char byte = line[++i];
                if (byte == '\a') break;
                if (byte == '\x1b' && i + 1 < line.size() && line[i + 1] == '\\') {
                    ++i;
                    break;
                }
            }
        }
    }
    return kept;
}

}  // namespace

tl::expected<void, ErrorPtr> runProgram(const std::vector<std::string>& command) {
    if (command.empty()) return {};
    return answer(command.front(), sys::runCommand(command));
}

tl::expected<void, ErrorPtr> runProgramWithoutTerminal(
    const std::vector<std::string>& command) {
    if (command.empty()) return {};
    return answer(command.front(), sys::runWithoutTerminal(command));
}

tl::expected<ProgramOutput, ErrorPtr> runProgramForOutput(
    const std::vector<std::string>& command) {
    if (command.empty()) return ProgramOutput{};
    sys::CommandOutput ran = sys::readOutputOf(command, kOutputLimit);
    auto started = answer(command.front(), {ran.stage, ran.reason});
    if (!started) return tl::make_unexpected(std::move(started).error());
    ProgramOutput output;
    output.bytes = std::move(ran.output);
    output.cut = ran.cut;
    return output;
}

std::vector<std::string> outputLines(const std::string& bytes,
                                     const std::string& charset) {
    encoding::IconvRecoder recoder;
    const std::string text = recoder.toUtf8(bytes, charset);

    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + start, end - start);
        while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (const size_t over = line.rfind('\r'); over != std::string_view::npos) {
            line.remove_prefix(over + 1);
        }
        lines.push_back(config::text::messageLine(withoutEscapes(line)));
        start = end + 1;
    }
    while (!lines.empty() && config::text::trim(lines.back()).empty()) lines.pop_back();
    return lines;
}

tl::expected<void, ErrorPtr> startProgram(const std::vector<std::string>& command) {
    if (command.empty()) return {};
    return answer(command.front(), sys::startDetached(command));
}

}  // namespace amberedit::app
