/// The program the tests run when they need a program to run.
///
/// Everything here was a `#!/bin/sh` script written into a temporary directory
/// and chmod'ed, which is three things Windows does not have: a shebang, an
/// execute bit, and /bin/sh. A compiled helper is the same helper everywhere.
///
/// It is also the better test. What `runProgram` promises is that nothing goes
/// through a shell — that `two words` stays one argument and `$HOME` and `*`
/// arrive as themselves. Asking a shell to prove that has always been slightly
/// beside the point; asking a program that reads `argv` proves it directly.
///
/// Modes, each named by the first argument:
///
///   nothing              exits, having done nothing — an editor closed with no
///                        change, or a program that just succeeds. Also what no
///                        arguments at all mean, which is how a shell the user
///                        typed `exit` into is stood in for.
///   args <out> [word...] writes each remaining argument to `out`, one per line
///   first <out> <word>   writes `word` to `out` with no newline after it
///   write <out> <text>   writes `text` to `out`, which is what an editor that
///                        saved something amounts to
///
/// In `text` the two characters `\n`, `\r` and `\t` stand for those characters
/// and `\\` for a backslash. Written that way rather than passed as themselves
/// so that no part of this depends on what a command line may carry.

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string unescaped(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        switch (text[++i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '\\': out += '\\'; break;
            default:
                out += '\\';
                out += text[i];
                break;
        }
    }
    return out;
}

/// Binary, so that a `\r` written here is a `\r` on disk: the text mode a
/// Windows runtime opens a stream in would turn every `\n` into `\r\n` and the
/// test for DOS line endings would be testing the runtime.
bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::vector<std::string> words(argv + (argc > 0 ? 1 : 0), argv + argc);
    if (words.empty()) return 0;

    const std::string& mode = words[0];

    if (mode == "nothing") return 0;

    if (mode == "args") {
        if (words.size() < 2) return 2;
        std::string text;
        for (size_t i = 2; i < words.size(); ++i) text += words[i] + "\n";
        return writeFile(words[1], text) ? 0 : 1;
    }

    if (mode == "first") {
        if (words.size() < 3) return 2;
        return writeFile(words[1], words[2]) ? 0 : 1;
    }

    if (mode == "write") {
        if (words.size() < 3) return 2;
        return writeFile(words[1], unescaped(words[2])) ? 0 : 1;
    }

    std::fprintf(stderr, "stub: no such mode: %s\n", mode.c_str());
    return 2;
}
