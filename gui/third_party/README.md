# Nuklear

Upstream: https://github.com/Immediate-Mode-UI/Nuklear
Pinned commit: `bab18497df988c171fd0d82baad798e1e0e34259`

`nuklear.h` has local clipboard fixes: copy measures UTF-8 bytes and
paste inserts byte lengths while advancing the cursor by runes. Nuklear is
available under either the MIT license or the public domain; the complete
upstream license notice is preserved in `Nuklear-LICENSE` and in the header.

LuaC uses the C99 implementation with a caller-owned fixed arena. The Studio
adapter converts Nuklear command lists into the existing Vulkan geometry. It
does not use the Lua VM, a JavaScript runtime, or per-frame Nuklear allocation.
