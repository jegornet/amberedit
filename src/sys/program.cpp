#include "sys/program.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <system_error>
#endif

namespace amberedit::sys {

std::filesystem::path executablePath() {
#ifdef _WIN32
    // GetModuleFileNameW truncates rather than failing, and says so only by
    // filling the buffer exactly, so the buffer grows until it comes back short.
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size()) return std::filesystem::path(buffer.data());
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);  // asks how much room it wants
    std::string buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    // Through weakly_canonical, since what this answers may hold `..` or run
    // through a symlink — and on macOS very often does.
    std::error_code ec;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(buffer, ec);
    return ec ? std::filesystem::path(buffer) : resolved;
#else
    std::error_code ec;
    const std::filesystem::path resolved =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : resolved;
#endif
}

}  // namespace amberedit::sys
