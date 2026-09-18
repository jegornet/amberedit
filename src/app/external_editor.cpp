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

tl::expected<ExternalEdit, ErrorPtr> runExternalEditor(
    const std::vector<std::string>& editor, const std::string& path,
    const std::vector<std::string>& lines, const std::string& charset) {
    encoding::IconvRecoder recoder;

    // A charset the message cannot be written in is a failure rather than a
    // file full of question marks — the export's reasoning exactly, and here
    // there is more at stake: what comes back out of the file *is* the message,
    // so a character lost on the way in is lost from the message itself. That
    // is why this goes through intoCharset and not the reader's fromUtf8.
    std::string handed;
    for (const std::string& line : lines) {
        auto encoded = recoder.intoCharset(line, charset);
        if (!encoded) return tl::make_unexpected(std::move(encoded).error());
        handed += *encoded;
        handed += '\n';
    }

    auto written = writeWhole(path, handed);
    if (!written) return tl::make_unexpected(std::move(written).error());

    auto ran = runProgram(commandWithMessageFile(editor, path));
    if (!ran) return tl::make_unexpected(std::move(ran).error());

    auto read = config::text::readFile(path);
    if (!read) return tl::make_unexpected(std::move(read).error());

    ExternalEdit edited;
    if (*read == handed) {
        // Not a byte moved. What that means about the message is the caller's
        // to say; all that is known here is that the file came back as it went.
        edited.lines = lines;
        return edited;
    }

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

tl::expected<ExternalEdit, ErrorPtr> runUtilOnMessage(
    const std::vector<std::string>& command, const std::string& path,
    const std::vector<std::string>& lines, const std::string& charset) {
    auto ran = runExternalEditor(command, path, lines, charset);
    // Taken away whatever came of it, the failures included: a charset that
    // could not be written leaves no file, and one the program could not be
    // started on leaves the message sitting in `tmpdir` for nobody.
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return ran;
}

}  // namespace amberedit::app
