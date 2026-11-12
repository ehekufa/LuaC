# Third-party runtime dependency

- `miniaudio.h`: miniaudio **0.11.26**, upstream commit
  `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6`, from
  <https://github.com/mackron/miniaudio>.
- `miniaudio-LICENSE`: upstream dual public-domain / MIT-0 license.

The unmodified single-header distribution is included so offline source and
release-archive builds can enable `LUAC_WITH_MINIAUDIO` without a download
step. LuaC uses its native playback device API only; PCM/WAV/DSP code is LuaC's
own implementation. No part of Lua or LuaJIT is included.

mbedTLS is deliberately not vendored: use an installed, supported mbedTLS 3.x
package and enable `LUAC_WITH_MBEDTLS` explicitly.
