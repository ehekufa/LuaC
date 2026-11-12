# Third-party runtime dependencies

- `miniaudio.h`: miniaudio **0.11.26**, upstream commit
  `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6`, from
  <https://github.com/mackron/miniaudio>.
- `miniaudio-LICENSE`: upstream dual public-domain / MIT-0 license.
- `stb_image.h`, `stb_truetype.h`: stb upstream commit
  `78770d85c4d94787a56c7859017da963bc4f3439`, from
  <https://github.com/nothings/stb>.
- `stb-LICENSE`: upstream public-domain / MIT license.

The unmodified single-header distributions are included so offline source and
release-archive builds can enable `LUAC_WITH_MINIAUDIO` and decode PNG/JPEG
assets without a download step. LuaC uses miniaudio's native playback device
API only; PCM/WAV/DSP code is LuaC's own implementation. stb_image supplies
image decoding, while stb_truetype rasterizes Studio's embedded IBM Plex Mono
font. No part of Lua or LuaJIT is included.

mbedTLS is deliberately not vendored: use an installed, supported mbedTLS 3.x
package and enable `LUAC_WITH_MBEDTLS` explicitly.
