"""Pressing keys into a Windows terminal, for the two Windows drivers.

Keys go through SendInput by virtual key with the scan code beside it and every
modifier held by hand, so that the terminal gets what a keyboard would have
given it. That needs the desktop of the user logged on at the console, which is
why both drivers are started there through a scheduled task that runs
interactively (`schtasks /it`) rather than from an ssh login.
"""

import ctypes
import ctypes.wintypes as wt
import queue
import threading
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP = 0x1, 0x2
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x2, 0x4
ULONG_PTR = ctypes.c_size_t


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _INPUTUNION)]


user32.SendInput.argtypes = [wt.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.GetForegroundWindow.restype = wt.HWND
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetKeyboardLayout.restype = ctypes.c_void_p
user32.MapVirtualKeyExW.argtypes = [wt.UINT, wt.UINT, ctypes.c_void_p]
user32.VkKeyScanExW.argtypes = [wt.WCHAR, ctypes.c_void_p]
user32.VkKeyScanExW.restype = ctypes.c_short
user32.SetForegroundWindow.argtypes = [wt.HWND]
user32.IsWindow.argtypes = [wt.HWND]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
user32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
user32.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
user32.IsWindowVisible.argtypes = [wt.HWND]
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]

VK = {
    "Backspace": 0x08, "Tab": 0x09, "Enter": 0x0D, "Esc": 0x1B, "Space": 0x20,
    "PgUp": 0x21, "PgDn": 0x22, "End": 0x23, "Home": 0x24,
    "Left": 0x25, "Up": 0x26, "Right": 0x27, "Down": 0x28,
    "Ins": 0x2D, "Del": 0x2E,
    **{f"F{n}": 0x6F + n for n in range(1, 13)},
}
# The keys of the block between the letters and the keypad: the same scan codes
# as the keypad's, told apart by the extended flag.
EXTENDED = {"PgUp", "PgDn", "End", "Home", "Left", "Up", "Right", "Down", "Ins", "Del"}
MODIFIERS = {"ctrl": 0xA2, "alt": 0xA4, "shift": 0xA0}  # the left-hand ones
NEVER_PRESSED = {"Alt-F4"}


def chord(name, layout):
    """The modifiers and the virtual key of a key named the way --keytest names
    it, the punctuation looked up in the layout the terminal is typing in."""
    mods = []
    rest = name
    while True:
        for prefix, mod in (("Ctrl-", "ctrl"), ("Alt-", "alt"), ("Shift-", "shift")):
            if rest.startswith(prefix) and len(rest) > len(prefix):
                mods.append(mod)
                rest = rest[len(prefix):]
                break
        else:
            break
    if rest in VK:
        return mods, rest, VK[rest]
    if len(rest) == 1 and rest.isalpha():
        if rest.isupper() and not mods:
            mods.append("shift")
        return mods, rest, ord(rest.upper())
    if len(rest) == 1:
        found = user32.VkKeyScanExW(rest, layout)
        if found == -1:
            raise ValueError(f"no key in this layout types {name!r}")
        if found & 0x100:
            mods.append("shift")
        return mods, rest, found & 0xFF
    raise ValueError(f"no virtual key for {name!r}")


def key_input(vk, up, extended, layout):
    flags = (KEYEVENTF_KEYUP if up else 0) | (KEYEVENTF_EXTENDEDKEY if extended else 0)
    scan = user32.MapVirtualKeyExW(vk, 0, layout)  # MAPVK_VK_TO_VSC
    event = INPUT(type=INPUT_KEYBOARD)
    event.u.ki = KEYBDINPUT(vk, scan, flags, 0, 0)
    return event


def send(events):
    array = (INPUT * len(events))(*events)
    if user32.SendInput(len(events), array, ctypes.sizeof(INPUT)) != len(events):
        raise OSError(ctypes.get_last_error(), "SendInput refused the events")


def press(name, layout):
    mods, key, vk = chord(name, layout)
    events = [key_input(MODIFIERS[m], False, False, layout) for m in mods]
    events.append(key_input(vk, False, key in EXTENDED, layout))
    events.append(key_input(vk, True, key in EXTENDED, layout))
    events += [key_input(MODIFIERS[m], True, False, layout) for m in reversed(mods)]
    send(events)
    time.sleep(0.05)


def click(hwnd):
    """A click in the middle of the window, which is what gives a window the
    keyboard when SetForegroundWindow is refused it."""
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    point = wt.POINT(rect.right // 2, rect.bottom // 2)
    user32.ClientToScreen(hwnd, ctypes.byref(point))
    user32.SetCursorPos(point.x, point.y)
    events = []
    for flag in (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP):
        event = INPUT(type=INPUT_MOUSE)
        event.u.mi = MOUSEINPUT(0, 0, 0, flag, 0, 0)
        events.append(event)
    send(events)


def windows():
    """Every visible top-level window: its handle, process, class and title."""
    found = []

    def each(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            cls = ctypes.create_unicode_buffer(256)
            title = ctypes.create_unicode_buffer(512)
            user32.GetClassNameW(hwnd, cls, 256)
            user32.GetWindowTextW(hwnd, title, 512)
            found.append((hwnd, pid.value, cls.value, title.value))
        return True

    user32.EnumWindows(WNDENUMPROC(each), 0)
    return found


def wait_for_window(test, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for hwnd, pid, cls, title in windows():
            if test(pid, cls, title):
                return hwnd
        time.sleep(0.2)
    return None


def layout_of(hwnd):
    return user32.GetKeyboardLayout(user32.GetWindowThreadProcessId(hwnd, None))


def empty_clipboard():
    if user32.OpenClipboard(None):
        user32.EmptyClipboard()
        user32.CloseClipboard()


class NotInFront(Exception):
    pass


def in_front(hwnd, what):
    """Brings the window forward before a key goes to it, clicking into it where
    SetForegroundWindow is refused, and stops the run where neither works: a key
    pressed into whatever else is in front is a key typed into somebody's work."""
    if user32.GetForegroundWindow() == hwnd:
        return
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    if user32.GetForegroundWindow() != hwnd:
        click(hwnd)
        time.sleep(0.3)
    if user32.GetForegroundWindow() != hwnd:
        raise NotInFront(f"the window is not in front at {what}; "
                         "stopped before pressing anything else")


def follow(path, finished):
    """The lines a file gains as it grows, from a thread of their own, and a
    function answering the next one within a timeout or None. The file is read
    to its end once `finished()` says nothing more will be written, and a line
    `end` is the end as well."""
    lines = queue.Queue()

    def reader():
        while True:
            try:
                f = open(path, "rb")
                break
            except FileNotFoundError:
                if finished():
                    lines.put(None)
                    return
                time.sleep(0.05)
        with f:
            pending = b""
            while True:
                chunk = f.read()
                if chunk:
                    pending += chunk
                    while b"\n" in pending:
                        line, _, pending = pending.partition(b"\n")
                        text = line.decode("utf-8", "replace").rstrip("\r")
                        if text == "end":
                            lines.put(None)
                            return
                        lines.put(text)
                elif finished():
                    break
                else:
                    time.sleep(0.02)
        lines.put(None)

    threading.Thread(target=reader, daemon=True).start()

    def next_line(timeout):
        try:
            return lines.get(timeout=timeout)
        except queue.Empty:
            return None

    return next_line
