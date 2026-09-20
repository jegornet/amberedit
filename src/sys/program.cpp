#include "sys/program.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>

#include <sys/sysctl.h>

#include <climits>
#include <system_error>
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
#elif defined(__FreeBSD__)
    // A sysctl and not /proc/self/exe, because FreeBSD mounts no procfs unless
    // somebody has asked it to and a stock system has not — so the Linux branch
    // below answers an empty path on a machine that knows perfectly well where
    // its own binary is. What that costs is the catalogs: i18n looks for them
    // beside the binary when the path compiled in is not where the tree ended
    // up, and an empty answer leaves that lookup with nowhere to go.
    //
    // The -1 is this process; KERN_PROC_PATHNAME writes a path and counts the
    // terminator in what it reports.
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    char buffer[PATH_MAX];
    size_t size = sizeof buffer;
    if (::sysctl(mib, 4, buffer, &size, nullptr, 0) != 0 || size == 0) return {};
    buffer[sizeof buffer - 1] = '\0';

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
