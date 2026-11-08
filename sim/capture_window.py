import ctypes, time, sys
from ctypes import wintypes
from PIL import Image

title = sys.argv[1] if len(sys.argv) > 1 else "LGFX Simulator"
out = sys.argv[2] if len(sys.argv) > 2 else r"C:\Users\96943\Desktop\esp32\car\sim\shot.png"
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

# Accept either a window title or a process id; PID is more reliable when
# the simulator is launched through a shell and the exact title has padding.
hwnd = 0
if title.isdigit():
    pid = int(title)
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def enum_proc(h, lparam):
        global hwnd
        proc_id = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(proc_id))
        if proc_id.value == pid and user32.IsWindowVisible(h):
            hwnd = h
            return False
        return True
    user32.EnumWindows(WNDENUMPROC(enum_proc), 0)
else:
    hwnd = user32.FindWindowW(None, title)
print("hwnd", hwnd)
if not hwnd:
    raise SystemExit(1)
user32.ShowWindow(hwnd, 9)
user32.SetForegroundWindow(hwnd)
time.sleep(0.35)
r = wintypes.RECT()
user32.GetWindowRect(hwnd, ctypes.byref(r))
w, h = r.right - r.left, r.bottom - r.top
print("rect", r.left, r.top, w, h)
hdc = user32.GetWindowDC(hwnd)
mdc = gdi32.CreateCompatibleDC(hdc)
bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
gdi32.SelectObject(mdc, bmp)
ok = user32.PrintWindow(hwnd, mdc, 2)
print("printwindow", ok)

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]

bi = BITMAPINFOHEADER()
bi.biSize = ctypes.sizeof(bi)
bi.biWidth = w
bi.biHeight = -h
bi.biPlanes = 1
bi.biBitCount = 32
bi.biCompression = 0
buf = ctypes.create_string_buffer(w * h * 4)
gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
img.convert("RGB").save(out)
print(out)
gdi32.DeleteObject(bmp)
gdi32.DeleteDC(mdc)
user32.ReleaseDC(hwnd, hdc)
