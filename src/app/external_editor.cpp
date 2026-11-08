#include "app/external_editor.hpp"

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "app/run_program.hpp"
#include "config/app_config.hpp"
#include "config/temp_dir.hpp"
#include "config/text_util.hpp"
#include "encoding/iconv_recoder.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::app {
namespace {

[[nodiscard]] tl::expected<void, ErrorPtr> writeWhole(const std::string& path,
                                                      const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return failure(i18n::format(_("cannot write file: {0}"), {path}));
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    // Asked again after the write: a full disk fails there and not on the open,
    // and a message half written to the file is a message half handed over.
    if (!file) return failure(i18n::format(_("cannot write file: {0}"), {path}));
    return {};
}

}  // namespace

tl::expected<std::string, ErrorPtr> externalEditPath(
    const std::string& configuredTempDir) {
    auto dir = config::makeTempDir(configuredTempDir);
    if (!dir) return tl::make_unexpected(std::move(dir).error());
    return *dir + "/amberedit-" + std::to_string(static_cast<long>(::getpid())) + ".msg";
}

std::vector<std::string> commandWithMessageFile(const std::vector<std::string>& words,
                                                const std::string& path) {
    const std::string_view mark = config::AppConfig::kMsgPlaceholder;
    std::vector<std::string> command;
    command.reserve(words.size());
    for (const std::string& word : words) {
        std::string filled = word;
        for (size_t at = filled.find(mark); at != std::string::npos;
             at = filled.find(mark, at + path.size())) {
            filled.replace(at, mark.size(), path);
        }
        command.push_back(std::move(filled));
    }
    return command;
}

bool namesMessageFile(const std::vector<std::string>& command) {
    const std::string_view mark = config::AppConfig::kMsgPlaceholder;
    return std::any_of(command.begin(), command.end(), [mark](const std::string& word) {
        return word.find(mark) != std::string::npos;
    });
}

tl::expected<std::string, ErrorPtr> externUtilMsgPath(
    const std::string& configuredTempDir) {
    auto dir = config::makeTempDir(configuredTempDir);
    if (!dir) return tl::make_unexpected(std::move(dir).error());
    return *dir + "/amberedit-" + std::to_string(static_cast<long>(::getpid())) +
           "-util.msg";
}

tl::expected<std::string, ErrorPtr> externUtilDetachedPath(
    const std::string& configuredTempDir) {
    auto dir = config::makeTempDir(configuredTempDir);
    if (!dir) return tl::make_unexpected(std::move(dir).error());
    const std::string stem =
        *dir + "/amberedit-" + std::to_string(static_cast<long>(::getpid())) + "-util-";
    // Counted up past whatever is there, an earlier session's left behind
    // under a process id that has come round again among it.
    for (unsigned number = 1;; ++number) {
        std::string path = stem + std::to_string(number) + ".msg";
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) && !ec) return path;
        if (ec) return failure(i18n::format(_("cannot write file: {0}"), {path}));
    }
}

namespace {

/// The lines as they go into the file, in `charset`.
///
/// A charset the message cannot be written in is a failure rather than a file
/// full of question marks — the export's reasoning exactly, and here there is
/// more at stake: what comes back out of the file *is* the message, so a
/// character lost on the way in is lost from the message itself. That is why
/// this goes through intoCharset and not the reader's fromUtf8.
[[nodiscard]] tl::expected<std::string, ErrorPtr> fileBytes(
    const std::vector<std::string>& lines, const std::string& charset) {
    encoding::IconvRecoder recoder;
    std::string handed;
    for (const std::string& line : lines) {
        auto encoded = recoder.intoCharset(line, charset);
        if (!encoded) return tl::make_unexpected(std::move(encoded).error());
        handed += *encoded;
        handed += '\n';
    }
    return handed;
}

/// What the file holds after the program, against `handed`, what was written
/// into it before.
[[nodiscard]] tl::expected<ExternalEdit, ErrorPtr> readBack(
    const std::string& path, const std::string& handed,
    const std::vector<std::string>& lines, const std::string& charset) {
    auto read = config::text::readFile(path);
    if (!read) return tl::make_unexpected(std::move(read).error());

    ExternalEdit edited;
    if (*read == handed) {
        // Not a byte moved. What that means about the message is the caller's
        // to say; all that is known here is that the file came back as it went.
        edited.lines = lines;
        return edited;
    }

    encoding::IconvRecoder recoder;
    auto decoded = recoder.intoUtf8(*read, charset);
    if (!decoded) return tl::make_unexpected(std::move(decoded).error());

    edited.changed = true;
    for (const std::string& line : config::text::splitLines(*decoded)) {
        // What a file may hold and a message may not, taken out here as it is
        // on the import: the editor is somebody else's program and puts a tab
        // in wherever it likes.
        edited.lines.push_back(config::text::messageLine(line));
    }
    return edited;
}

}  // namespace

tl::expected<ExternalEdit, ErrorPtr> runExternalEditor(
    const std::vector<std::string>& editor, const std::string& path,
    const std::vector<std::string>& lines, const std::string& charset) {
    auto handed = fileBytes(lines, charset);
    if (!handed) return tl::make_unexpected(std::move(handed).error());

    auto written = writeWhole(path, *handed);
    if (!written) return tl::make_unexpected(std::move(written).error());

    auto ran = runProgram(commandWithMessageFile(editor, path));
    if (!ran) return tl::make_unexpected(std::move(ran).error());

    return readBack(path, *handed, lines, charset);
}

tl::expected<ExternalEdit, ErrorPtr> runUtilOnMessage(
    const std::vector<std::string>& command, config::ExternUtil::Run run,
    const std::string& path, const std::vector<std::string>& lines,
    const std::string& charset) {
    using Run = config::ExternUtil::Run;
    auto ran = [&]() -> tl::expected<ExternalEdit, ErrorPtr> {
        auto handed = fileBytes(lines, charset);
        if (!handed) return tl::make_unexpected(std::move(handed).error());
        auto written = writeWhole(path, *handed);
        if (!written) return tl::make_unexpected(std::move(written).error());

        const std::vector<std::string> words = commandWithMessageFile(command, path);
        ProgramOutput output;
        if (run == Run::PrintResult) {
            auto printed = runProgramForOutput(words);
            if (!printed) return tl::make_unexpected(std::move(printed).error());
            output = std::move(*printed);
        } else {
            auto started = run == Run::Terminal     ? runProgram(words)
                           : run == Run::NoTerminal ? runProgramWithoutTerminal(words)
                                                    : startProgram(words);
            if (!started) return tl::make_unexpected(std::move(started).error());
        }

        if (run == Run::Detached) {
            ExternalEdit untouched;
            untouched.lines = lines;
            return untouched;
        }
        auto back = readBack(path, *handed, lines, charset);
        if (back) back->output = std::move(output);
        return back;
    }();
    // Left where it is for a program that is still running and is reading it,
    // which is a detached one that started — the file is its from here on.
    if (run == Run::Detached && ran) return ran;
    // Taken away whatever came of it otherwise, the failures included: a
    // charset that could not be written leaves no file, and one the program
    // could not be started on leaves the message sitting in `tmpdir` for
    // nobody.
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return ran;
}

}  // namespace amberedit::app
