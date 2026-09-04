#include "ui/error_log.hpp"

#include <array>
#include <ctime>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

#include "sys/time.hpp"

namespace amberedit::ui::error_log {

namespace {

std::string& logPath() {
    static std::string path;
    return path;
}

/// The stamp at the head of a line: local time to the second, written the way
/// that sorts. No zone in it — the file is read on the machine that wrote it.
std::string stamp() {
    const std::time_t now = std::time(nullptr);
    const std::tm broken = sys::localTime(now);
    std::array<char, 32> text{};
    const size_t written =
        std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M:%S", &broken);
    return std::string(text.data(), written);
}

/// The text with its line breaks turned into spaces, so that an exception which
/// speaks in paragraphs still takes one line of the log.
std::string oneLine(std::string text) {
    for (char& c : text) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return text;
}

}  // namespace

void open(std::string path) {
    logPath() = std::move(path);
}

const std::string& path() {
    return logPath();
}

void write(const std::string& where, const std::string& what) {
    if (logPath().empty()) return;
    // Opened and closed around the one line rather than held open for the
    // session: a run that throws nothing never touches the file, one that does
    // can be watched with `tail -f` while it runs, and nothing has to be flushed
    // on the way out of a program that may be leaving in a hurry.
    std::ofstream out(logPath(), std::ios::app);
    if (!out) return;
    out << stamp() << " " << oneLine(where) << ": " << oneLine(what) << "\n";
}

}  // namespace amberedit::ui::error_log
