#!/bin/bash
#
# Holds a Windows build to the promise the release zip makes: unpack it, run
# bin\amberedit.exe, and nothing has to be installed first.
#
#   tools/check-w64-imports.sh <exe> [<objdump>]
#
# The link is static (AMBEREDIT_STATIC in CMakeLists.txt), but "static" is not
# something the .exe records and not something the linker warns about failing to
# be: a dependency found as a `.dll.a` import library links without complaint and
# leaves behind an .exe that asks for libiconv-2.dll, libintl-8.dll and
# zlib1.dll on the machine it is run on. That went out in a release. What the
# .exe *does* record is its import table, so that is what is read here, and
# anything in it that Windows does not itself ship is an error.
#
# The list below is what Windows has: the api-ms-win-crt-* set is the Universal
# CRT, and the rest are the Win32 DLLs a console program reaches — user32 and
# advapi32 and winmm come in with PDCursesMod's wincon port. A name not on it is
# not necessarily wrong, but it is a new dependency on the build host and has to
# be looked at rather than shipped, so this fails and says which.
set -euo pipefail

exe=${1:-}
if [[ -z $exe ]]; then
  echo "usage: $0 <exe> [<objdump>]" >&2
  exit 2
fi

objdump=${2:-}
if [[ -z $objdump ]]; then
  for candidate in x86_64-w64-mingw32-objdump llvm-objdump objdump; do
    if command -v "$candidate" >/dev/null 2>&1; then
      objdump=$candidate
      break
    fi
  done
fi
if [[ -z $objdump ]]; then
  echo "check-w64-imports: no objdump on PATH" >&2
  exit 2
fi

imports=$("$objdump" -p "$exe" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr 'A-Z' 'a-z' | sort -u)
if [[ -z $imports ]]; then
  echo "check-w64-imports: $objdump read no import table out of $exe" >&2
  exit 2
fi

system='^(api-ms-win-.*|ucrtbase|kernel32|kernelbase|ntdll|user32|advapi32|winmm|shell32|shlwapi|ole32|oleaut32|gdi32|version|ws2_32|crypt32|bcrypt|imm32|comdlg32|comctl32|msvcrt)\.dll$'

foreign=$(echo "$imports" | grep -Ev "$system" || true)
if [[ -n $foreign ]]; then
  echo "check-w64-imports: $exe needs DLLs that are not part of Windows:" >&2
  echo "$foreign" | sed 's/^/  /' >&2
  echo >&2
  echo "A user unpacking the zip does not have these. They come of a dependency" >&2
  echo "found as its .dll.a import library instead of its .a archive — see" >&2
  echo "AMBEREDIT_STATIC in CMakeLists.txt." >&2
  exit 1
fi

echo "check-w64-imports: $exe needs nothing but Windows —"
echo "$imports" | sed 's/^/  /'
