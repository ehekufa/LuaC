"""Read two actual X11 window pixels, not the renderer's source geometry."""
import ctypes
import sys

x = ctypes.CDLL("libX11.so.6")
x.XOpenDisplay.argtypes = [ctypes.c_char_p]
x.XOpenDisplay.restype = ctypes.c_void_p
x.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int,
                       ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
x.XGetImage.restype = ctypes.c_void_p
x.XGetPixel.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
x.XGetPixel.restype = ctypes.c_ulong
x.XDestroyImage.argtypes = [ctypes.c_void_p]
x.XCloseDisplay.argtypes = [ctypes.c_void_p]
display = x.XOpenDisplay(None)
if not display:
    raise SystemExit("Cannot open X11 display")
window = int(sys.argv[1], 0)
def pixel(y):
    image = x.XGetImage(display, window, 40, y, 1, 1, ctypes.c_ulong(-1).value, 2)
    if not image:
        raise SystemExit("Cannot read Vulkan window")
    value = x.XGetPixel(image, 0, 0) & 0xffffff
    x.XDestroyImage(image)
    return value
try:
    top, bottom = pixel(40), pixel(180)
    if top != 0xff0000 or bottom != 0x00ff00:
        raise SystemExit(f"Orientation/clipping wrong: top={top:06x} bottom={bottom:06x}")
    print("Vulkan screen orientation and clipping: OK")
finally:
    x.XCloseDisplay(display)
