# LuaC

**LuaC (Lua Compiler)** — небольшой самостоятельный компилятор программ на Lua-подобном языке в машинный код. Исходные файлы используют обычное расширение **`.lua`**. Код оригинального Lua, его VM и интерпретатор не используются.

Есть два пути выполнения одного и того же сгенерированного C-кода:

- **JIT** — во время запуска создаётся разделяемая библиотека, загружается через `dlopen`, после чего управление получает native-код;
- **AOT** — создаётся обычный самостоятельный исполняемый файл.

Это не транслятор в исходник Lua и не обёртка над `liblua`.

## Быстрый старт

Нужен компилятор C с поддержкой GNU C99. Поддерживаются Linux, macOS и Windows/MinGW; JIT использует native shared library, AOT — обычный executable. Зависимостей от Lua и LuaJIT нет.

```sh
make
./luac examples/demo.lua       # JIT и запуск
```

AOT-сборка:

```sh
./luac --aot examples/demo.lua -o demo
./demo
```

Другие команды:

```sh
./luac --check program.lua                 # только синтаксис
./luac --emit-c program.lua -o program.c  # посмотреть результат компиляции
./luac --keep-c generated.c program.lua   # JIT, сохранив промежуточный C
./luac --cc clang program.lua             # выбрать C-компилятор
./luac --cflags "-DMY_DEFINE" --ldflags "-lmylib" program.lua
```

## HTTPS и native audio

Базовая сборка не требует внешних libraries. HTTPS включается установленным **mbedTLS 3.x**, playback — bundled **miniaudio 0.11.26**:

```sh
# Debian/Ubuntu: sudo apt install libmbedtls-dev
make MBEDTLS=1 MINIAUDIO=1
./luac examples/https.lua
./luac examples/audio.lua

# macOS/Homebrew (mbedtls keg-only):
make MBEDTLS=1 MINIAUDIO=1 \
  MBEDTLS_CFLAGS="-I$(brew --prefix mbedtls)/include" \
  MBEDTLS_LDFLAGS="-L$(brew --prefix mbedtls)/lib -lmbedtls -lmbedx509 -lmbedcrypto"

# Эквивалентная CMake конфигурация:
cmake -S . -B build -DLUAC_WITH_MBEDTLS=ON -DLUAC_WITH_MINIAUDIO=ON
cmake --build build -j2
```

Для нестандартного prefix в Make можно передать `MBEDTLS_CFLAGS`/`MBEDTLS_LDFLAGS`. Любой JIT/AOT вызов также принимает `--cflags` и `--ldflags`; переменные `LUAC_CFLAGS`/`LUAC_LDFLAGS` добавляют флаги глобально:

```sh
./luac --cflags=-DLUAC_WITH_MBEDTLS \
  --ldflags="-lmbedtls -lmbedx509 -lmbedcrypto" examples/https.lua
./luac --cflags=-DLUAC_WITH_MINIAUDIO --ldflags="-ldl -pthread" examples/audio.lua
```

HTTPS по умолчанию обязательно проверяет certificate chain и hostname через системные CA. Неудачный handshake никогда не повторяется как HTTP. `http.setca(path)` выбирает private CA bundle. `http.setverify(false)` существует только как явный opt-out и не должен использоваться в production.

Audio buffers содержат native interleaved float32 PCM. Доступны WAV PCM 8/16/24/32-bit и float32, sine/square/saw/triangle/noise synthesis, envelopes, mixing, gain/fade/normalize, filters, delay и linear resampling. `audio.playback_available()` сообщает только о наличии compiled backend; `play()` может вернуть `nil, message`, если устройства нет. WAV/DSP остаются рабочими на headless CI.

## Память без tracing GC

В runtime нет фонового или stop-the-world сборщика мусора. Компилятор автоматически создаёт cleanup-код — изменять `.lua` и вручную добавлять `free` не нужно.

- Каждая локальная переменная хранится в ownership-cell.
- Замыкания захватывают те же cells, поэтому upvalues сохраняют ссылочную семантику Lua.
- Строки, таблицы, функции и tuples имеют счётчик владельцев и освобождаются при потере последнего владельца.
- Все возвраты, ранние выходы и runtime-ошибки проходят через сгенерированный `lc_done`, где освобождаются locals и временные значения.
- Контекст ведёт список аллокаций как страховку. При завершении программы он освобождает даже циклические таблицы, не обходя их дочерние ссылки.

Таким образом, утечек при завершении контекста нет. Важное различие: циклы ссылок внутри **ещё работающей** программы освобождаются при завершении контекста, а не в момент их создания. Никакого периодического цикла сборки мусора при этом не запускается.

Фрагмент сгенерированного кода:

```c
lc_done:
    if (l0) { lc_cell_release(ctx, l0); l0 = NULL; }
    LC_RELEASE(ctx, t0);
    LC_RELEASE(ctx, t1);
    return lc_return_value;
```

## Что уже поддерживается

- `nil`, boolean, числа, строки, escape-последовательности и длинные строки;
- локальные и глобальные переменные, множественное присваивание;
- арифметика `+ - * / // % ^`, сравнения, `#`, `..`, `and`, `or`, `not`;
- bitwise `& | ~ << >>`, unary `~`, включая соответствующие metamethods;
- `if / elseif / else`, `while`, `repeat`, `break`, `goto` и `::labels::`;
- числовой и generic `for`;
- функции, рекурсия, анонимные функции, методы `:`, замыкания и изменяемые upvalues;
- varargs `...` и множественные возвращаемые значения;
- таблицы, array/named/keyed-поля, `pairs`, `ipairs`, `next`;
- metatables: `__index`, `__newindex`, `__call`, `__len`, `__tostring`, `__pairs`, arithmetic/comparison/concat/bitwise metamethods;
- raw API, `_G`, `pcall`, `xpcall`, `assert`, `error`, `select`, `type`, `tostring`, `tonumber`;
- `string`: byte/char/sub/rep/reverse/case/format, собственный Lua pattern engine для `find`, `match`, `gmatch`, `gsub` (classes, captures, sets, quantifiers, anchors, backreferences, `%b`, `%f`);
- `table`: insert/remove/concat/pack/unpack/move/sort;
- `math`: базовые и hyperbolic functions, fmod/modf/frexp/ldexp, random, conversion helpers;
- LuaJIT `bit`, Lua-compatible `bit32`, строгий UTF-8 через `utf8.char/len/codepoint/offset/codes`;
- файлы и процессы через `io.open/read/write/lines/seek/setvbuf/popen`, стандартные потоки и детерминированное закрытие;
- `os.date/time/difftime/getenv/remove/rename/tmpname/execute/exit/setlocale`;
- native modules через `package.preload`, `package.loaded` и `require`;
- **14 дополнительных native-модулей / 177 API**: `text`, `hex`, `base64`, `url`, `json`, `hash`, `fs`, `path`, `time`, `vec`, `socket`, `http`, `audio`, `engine`;
- TCP/UDP/DNS, verified HTTPS через mbedTLS, PCM/WAV/synthesis/mixing/effects и optional native playback доступны и в JIT, и в AOT;
- binary files, JSON Unicode, SHA-256/HMAC, vector math и scene simulation также работают без внешних dependencies;
- `collectgarbage` как compatibility API для метрик/состояния, без запуска скрытого collector.

Полный список функций и ограничений — [`docs/LIBRARIES.md`](docs/LIBRARIES.md). Готовые примеры: [`examples/data.lua`](examples/data.lua), [`examples/network.lua`](examples/network.lua), [`examples/game.lua`](examples/game.lua). Встроенные модули одновременно доступны как globals и через `require("json")`, `require("socket")` и т.д.

Пример — обычный `.lua`:

```lua
local function counter(start)
  local value = start
  return function(step)
    value = value + step
    return value
  end
end

local next = counter(10)
print(next(5), next(2)) -- 15  17
```

## Границы текущей версии

Это работающий native compiler, **но ещё не полная drop-in замена Lua 5.4/LuaJIT**. Текущая матрица честно фиксирует границы:

| Возможность | Статус |
|---|---|
| Lua VM, bytecode VM, AST interpreter | Нет и не планируется |
| Native JIT/AOT | Работает |
| Metatables, bitwise, `goto` | Работают |
| Coroutines/`yield` | Ещё не реализованы |
| Runtime `load`, `loadstring`, `dofile` | Ещё не реализованы; compilation происходит через compiler API/CLI |
| `require` | Работает для `package.preload`; динамический поиск и загрузка файлов пока не подключены |
| LuaJIT FFI | Ещё не реализован |
| HTTPS/TLS | Настоящий mbedTLS, CA и hostname verification; optional build dependency |
| Audio | Native PCM/WAV/DSP работают; playback через miniaudio включается опционально |
| Engine | Native 2D world/simulation и Studio viewport работают; physics solver/asset pipeline не заявляются готовыми |
| `debug` library, `string.dump`, `string.pack/unpack/packsize` | Ещё не реализованы |
| Раздельные integer/float subtypes | Пока единый `double`; bitwise преобразует к integer по правилам runtime |
| `math.maxinteger/mininteger` | Безопасный exactly representable диапазон ±2^53−1, не полный int64 |
| JIT tracing LuaJIT | Не копируется; LuaC компилирует функции в обычный машинный код через C compiler |

То есть LuaC пока не запустит любой Lua/LuaJIT-проект без изменений. Отсутствующие возможности не маскируются заглушками. Исходники Lua/LuaJIT могут изучаться как поведенческий референс, но не копируются, не вендорятся и не линкуются.

## Готовые сборки через GitHub Actions

Workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) запускается на `push`, `pull_request` и вручную через **Actions → Native builds → Run workflow**. После успешного прогона артефакты доступны на странице workflow:

| Артефакт | Содержимое |
|---|---|
| `LuaC-source` | Полный архив именно нашей реализации для копирования в свой build |
| `LuaC-windows-x86_64` | `luac.exe`, статическая и динамическая библиотеки, headers |
| `LuaC-linux-x86_64` | CLI, `libluac.a`, `libluac.so`, headers |
| `LuaC-macos-arm64` | CLI, `libluac.a`, `libluac.dylib`, headers |
| `LuaC-Studio-windows-x86_64` | Vulkan GUI `.exe`, shaders, GLFW DLL |
| `LuaC-Studio-linux-x86_64` | Vulkan GUI, CLI, shaders |
| `LuaC-Studio-Android-APK` | устанавливаемый debug APK с Java-редактором и Vulkan Surface |

Для JIT/AOT на desktop нужен системный C-компилятор (`cc`, GCC или Clang). В APK синтаксическая проверка и генерация C выполняются внутри нативной библиотеки; запуск JIT на Android не требуется и не выполняется, поскольку Android не поставляется с host C toolchain.

## LuaC Studio — Vulkan GUI

Desktop-редактор использует **Nuklear (C99, Immediate Mode) + собственный Vulkan renderer**. GLFW создаёт окно и передаёт input в Nuklear; layout, кнопки, multiline editor, selection, undo/redo, clipboard и scrolling обрабатывает Nuklear. Адаптер `gui/nuklear_canvas.hpp` преобразует его draw commands в geometry с Vulkan scissor-батчами.

Для Nuklear один раз выделяется фиксированная arena размером 4 MiB: скрытого рантайма, JavaScript-движка, Lua VM и выделений Nuklear в каждом кадре нет. В Studio настроены тёмная тема, округлые controls и цветовые hover/active-состояния. Внешний вид не зависит от JavaFX или CSS.

Типичный Immediate Mode код выглядит так:

```c
if (nk_begin(&ui, "Toolbar", nk_rect(0, 0, width, 64), NK_WINDOW_BACKGROUND)) {
    nk_layout_row_dynamic(&ui, 38, 1);
    if (nk_button_label(&ui, "Run")) run();
}
nk_end(&ui);
```

Зависимость закреплена в `gui/third_party/nuklear.h`; исходный upstream commit и лицензия указаны рядом с header. Bitmap-renderer сохраняет UTF-8 в файле, но пока отображает не-ASCII символы как `?`; полноценный font atlas можно добавить отдельно без изменения языка или compiler runtime.

```sh
# Debian/Ubuntu
sudo apt install libglfw3-dev libvulkan-dev glslang-tools pkg-config
make gui
./build/luac-gui examples/demo.lua
```

Горячие клавиши:

- **F5** — сохранить при необходимости и запустить через JIT;
- **F6** — проверить синтаксис;
- **F7** — переключить Editor/Engine;
- **Space в Engine** — play/pause simulation;
- **Ctrl+S** — сохранить;
- **Ctrl+C / Ctrl+X / Ctrl+V** — clipboard;
- **Ctrl+Z / Ctrl+Y** — undo / redo;
- мышь, колёсико и клавиатура — редактирование исходника.

Пути можно переопределить переменными `LUAC_COMPILER`, `LUAC_SHADER_DIR` и `LUAC_RUNTIME`. В пакетах shaders находятся в `build/`, поэтому GUI следует запускать из корня распакованного пакета.

### Engine mode в Studio

Кнопка **Engine** (или F7) открывает scene workspace: hierarchy, Vulkan viewport и inspector. Можно добавлять, дублировать, выбирать и удалять entities; редактировать position, velocity, size, rotation, scale, color, active/kinematic; запускать play/pause, отдельный fixed step и reset. Viewport отображает grid, sprites, selection и collisions, simulation использует deterministic 60 Hz fixed-step core.

Это не отдельный закрытый редактор: тот же native world доступен в Lua через `engine`, а C++17 header `src/engine.hpp` подключается к любому проекту через **`LuaC::engine` без Vulkan и без Lua runtime**. Пример — `examples/embed/engine_main.cpp`.

Android-версия находится в `android/`. При запуске открывается **Snake**: управление свайпами и экранными стрелками, очки, ускорение и рестарт касанием. Кнопка **LUAC EDITOR** открывает редактор, проверку, просмотр сгенерированного C и Vulkan Surface. Vulkan и native library инициализируются защищённо, поэтому их отсутствие не мешает игре; Vulkan не является обязательным для установки APK. Сборка из workflow использует Android SDK 35, NDK 27 и CMake.

## Библиотеки и C API

```sh
make libraries
# build/libluac.a
# build/libluac.so       # .dll на Windows, .dylib на macOS
```

Публичный интерфейс — [`src/luac.h`](src/luac.h):

```c
#include "luac.h"

char *error = NULL;
char *c = luac_compile_c("print('hello')", "hello.lua", &error);
if (c) {
    /* сохранить c и скомпилировать вместе с runtime.h */
}
luac_free(c);
luac_free(error);
```

Также доступны `luac_check`, `luac_version` и `luac_free`. Тот же API используется Android JNI и может подключаться к C/C++-проектам.

## Копирование в собственный build

Lua/LuaJIT не скачиваются и не подключаются. Для compiler + всех native libraries скопируйте `src/luac.c`, `src/luac.h`, `src/runtime.h`, `src/luac_libraries.h`, `src/luac_tls.h`, `src/luac_audio.h` и `src/third_party/`. Без playback miniaudio header не компилируется, но должен быть доступен в source distribution. Для C++ engine отдельно достаточно `src/engine.hpp`:

```sh
cc -std=gnu99 -DLUAC_LIBRARY_ONLY -DLUAC_RUNTIME_PATH=\"runtime.h\" \
  -I path/to/luac/src -c path/to/luac/src/luac.c -o luac.o
cc your_program.c luac.o -o your_program
```

Для generated C добавьте include path к каталогу с `runtime.h` и `-DLUAC_STANDALONE`, если нужен автоматический `main`. На Unix добавьте `-lm`, на Windows/MinGW — `-lws2_32`; CLI делает это автоматически.

### CMake `add_subdirectory`

Скопируйте репозиторий или каталог LuaC в проект:

```cmake
add_subdirectory(third_party/LuaC)
target_link_libraries(your_target PRIVATE LuaC::luac)
# LuaC::compiler также доступен для custom commands с --emit-c/--aot.
# Для независимого C++17 scene engine: LuaC::engine (header-only, без Vulkan).
```

### Установленный CMake package

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j2
cmake --install build-cmake --prefix /your/prefix
```

В другом проекте:

```cmake
find_package(LuaC 0.5 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE LuaC::luac)
# Generated native runtime и его optional dependencies:
target_link_libraries(your_generated_target PRIVATE LuaC::runtime)
# Опционально: target_link_libraries(your_game PRIVATE LuaC::engine)
```

Передайте `-DCMAKE_PREFIX_PATH=/your/prefix`. Установленный CLI ищет `../share/luac/runtime.h`, рядом устанавливается `luac_libraries.h`; library генерирует переносимый `#include "runtime.h"`. Абсолютный build path в чужой проект не требуется. Дополнительно устанавливается `lib/pkgconfig/luac.pc`:

```sh
cc your_program.c $(pkg-config --cflags --libs luac) -o your_program
```

Полный consumer example находится в [`examples/embed`](examples/embed): он проверяет и `LuaC::luac`, и custom command `LuaC::compiler`, и запуск native executable, собранного из generated C. Артефакт `LuaC-source` содержит весь наш source tree через `git archive`, без upstream Lua/LuaJIT.

## Тесты

```sh
make test
SANITIZE=1 make test  # дополнительно ASan/UBSan, если компилятор их поддерживает
```

`tests/features.lua`, `tests/compat.lua`, `tests/libraries.lua` и `tests/audio.lua` прогоняются через JIT и AOT. Расширенный suite проверяет data/text/fs/path/vector/engine functions, SHA-256/HMAC и реальные TCP/UDP loopback exchanges без внешней сети. Sanitizer job запускает generated native code под ASan/UBSan/LeakSanitizer. Отдельные headless tests проверяют Nuklear/Vulkan geometry и C++ world/components/fixed-step simulation. CMake/CTest повторяет проверки и дополнительно собирает compiler/runtime/engine consumers после install. Конфигурация с mbedTLS запускает `tests/run_https.sh`: локальный self-signed server должен быть отклонён, явно доверенный CA принят, а hostname mismatch отклонён и в JIT, и в AOT.

## Устройство проекта

- `src/luac.c` — lexer, parser, AST, генератор C, JIT-loader, C API и CLI;
- `src/luac.h` — публичный API библиотеки;
- `src/runtime.h` — значения, metatables, файлы, sockets и ownership runtime;
- `src/luac_libraries.h` — самостоятельные data/network/system/engine native modules;
- `src/luac_tls.h` — mbedTLS transport и certificate verification;
- `src/luac_audio.h`, `src/third_party/miniaudio.h` — PCM/WAV/DSP и optional playback;
- `src/engine.hpp` — header-only C++17 world/scene library для Studio и embedding;
- `docs/LIBRARIES.md` — справочник по 177 новым API;
- `CMakeLists.txt`, `cmake/` — build/install/export package и AOT CTest;
- `examples/embed/` — независимый consumer для CMake package;
- `gui/` — Nuklear editor, command→geometry adapter, desktop Vulkan renderer и GLSL shaders;
- `gui/third_party/` — pinned Nuklear single-header distribution и лицензия;
- `android/` — APK, Java UI, JNI и Android Vulkan renderer;
- `.github/workflows/build.yml` — тесты и кроссплатформенные артефакты;
- `examples/demo.lua` — пример;
- `tests/features.lua`, `tests/compat.lua` — проверка языка, runtime и стандартных библиотек.

Лицензия проекта находится в `LICENSE`.
