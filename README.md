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

### Windows и TinyCC

В Windows x86-64 архиве находится закреплённая сборка [TinyCC](https://github.com/tinycc/tinycc) вместе с необходимыми headers и библиотеками: `luac.exe` автоматически использует её, если не задан `CC` или `--cc`. Поэтому для обычных JIT/AOT запусков отдельный GCC не нужен. TinyCC распространяется под LGPL; его исходный архив и лицензия включены в комплект. Вы можете явно выбрать другой компилятор через `--cc` или `CC`.

`--emit-c` создаёт C-файл с `main`, который подключает `runtime.h` и соседние headers из установленного LuaC; это не один самодостаточный файл. Например, в Linux: `cc program.c -o program -lm -ldl`. Для Windows используйте комплектный `tcc\tcc.exe program.c -o program.exe -lm -lws2_32`; перенос C-файла на другую машину требует перенести runtime headers и исправить путь в `#include`. Дополнительные TLS/audio возможности требуют соответствующих флагов и библиотек. TinyCC ускоряет компиляцию, но не гарантирует ускорения полученной программы: сгенерированный код всё ещё использует динамический runtime.

## HTTPS и native audio

Базовая сборка не требует внешних libraries. HTTPS включается установленным **mbedTLS 2.28+, 3.x или 4.x**, playback — bundled **miniaudio 0.11.26**:

```sh
# Debian/Ubuntu: sudo apt install libmbedtls-dev
make MBEDTLS=1 MINIAUDIO=1
./luac examples/https.lua
./luac examples/audio.lua

# macOS/Homebrew (LTS 3.x, keg-only): brew install mbedtls@3
make MBEDTLS=1 MINIAUDIO=1 \
  MBEDTLS_CFLAGS="-I$(brew --prefix mbedtls@3)/include" \
  MBEDTLS_LDFLAGS="-L$(brew --prefix mbedtls@3)/lib -lmbedtls -lmbedx509 -lmbedcrypto"

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

## Ownership и cycle GC

Компилятор автоматически создаёт cleanup-код — изменять `.lua` и вручную добавлять `free` не нужно. Локальные переменные и замыкания используют ownership-cells, поэтому upvalues сохраняют ссылочную семантику Lua.

- Строки, таблицы, функции, tuples и native objects имеют счётчик владельцев.
- Все возвраты, ранние выходы и runtime-ошибки проходят через сгенерированный `lc_done`.
- Периодический collector анализирует внутренние и внешние ссылки, сохраняет живые C locals/upvalues и освобождает недостижимые циклы во время выполнения.
- `setmetatable(t, {__mode = "k"})`, `"v"` и `"kv"` создают weak tables; мёртвые weak entries удаляются при collection.
- `collectgarbage("collect"/"step"/"stop"/"restart"/"isrunning"/"count")` управляет collector и метриками. Фоновых threads и stop-the-world пауз отдельного GC thread нет.

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
- native modules через `package.preload`, `package.loaded` и `require`; статические `require("module")` автоматически находят соседние `.lua`/`init.lua` и связываются в JIT/AOT binary;
- **17 дополнительных native-модулей / 197 API**: `text`, `hex`, `base64`, `url`, `json`, `hash`, `fs`, `path`, `time`, `vec`, `socket`, `http`, `audio`, `engine`, `coroutine`, `ffi`, `websocket`;
- TCP/UDP/DNS, verified HTTPS и WebSocket client/server (`ws`/`wss`) через mbedTLS, PCM/WAV/synthesis/mixing/effects и optional native playback доступны и в JIT, и в AOT;
- binary files, JSON Unicode, SHA-256/HMAC, vector math и scene simulation также работают без внешних dependencies;
- signed int64 и float subtypes, stackful `coroutine.create/resume/yield/wrap`, periodic cycle collection и weak tables.

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
| Coroutines/`yield` | Stackful native stacks: `ucontext` на Unix и Fibers на Windows; работают в JIT/AOT |
| Runtime `load`, `loadstring`, `dofile` | Ещё не реализованы; compilation происходит через compiler API/CLI |
| `require` | `package.preload` и compile-time связывание literal disk imports; динамический module name требует preload |
| FFI | Базовый `ffi.open/close/call`, typed signatures через system libffi runtime; structs/callbacks/varargs не поддержаны |
| HTTPS/TLS/WebSocket | Настоящий mbedTLS, CA и hostname verification; `ws`/`wss` client и server, optional TLS build dependency |
| Audio | Native PCM/WAV/DSP работают; playback через miniaudio включается опционально |
| Engine | Native 2D world, opt-in sequential AABB physics, PNG/JPEG assets, input и UTF-8 text работают; это не rotation-aware rigid-body engine |
| `debug` library, `string.dump`, `string.pack/unpack/packsize` | Ещё не реализованы |
| Раздельные integer/float subtypes | Signed int64 literals/arithmetic/keys/bitwise и float subtype; integer overflow использует two’s-complement wrap |
| `math.maxinteger/mininteger` | Полный диапазон `INT64_MAX`/`INT64_MIN` |
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
| `LuaC-Studio-Android-APK` | устанавливаемый APK с проектами, многофайловым runtime, assets и game viewport |

Для JIT/AOT на desktop нужен системный C-компилятор (`cc`, GCC или Clang). В APK синтаксическая проверка и генерация C выполняются внутри нативной библиотеки. **RUN** использует встроенный AST runtime поверх тех же native libraries, поэтому проекты запускаются на телефоне без host C toolchain.

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

Android-версия находится в `android/`. При запуске открывается **список проектов**. Новый проект содержит `main.lua`, вложенные Lua-модули и PNG в `assets/`; можно создавать дополнительные файлы и папки, редактировать их, импортировать PNG/JPG через системный picker и удалять лишнее. Файлы, папки и проекты переименовываются через меню «⋮ → Rename», а открытый файл — кнопкой с карандашом. Переименование не исправляет пути и `require()` в коде автоматически; для запуска нужен файл `main.lua`. **RUN** выполняет `main.lua`, поддерживает disk `require()` между файлами и открывает игровой viewport с `print()`/ошибками в консоли. Глобальные `world` и `update(dt)` подключаются к frame loop: entities, textures, physics, camera, touch input и overlay text обновляются каждый кадр. **STOP / EDIT** завершает runtime и освобождает контекст. Проекты хранятся в приватной папке APK и не требуют разрешения на общее хранилище. Минимальная версия — Android 8 / API 26. Stackful coroutines используют переносимый pthread backend, поскольку Bionic не предоставляет ucontext-switch APIs. Сборка из workflow использует Android SDK 35, NDK 27 и CMake.

### APK проекта через Android Studio

В меню файлов выберите **Build project APK** и место сохранения. Studio помещает файлы проекта в отдельное приложение с embedded runtime и подписывает APK локальным ключом Android Keystore; внешний компилятор на телефоне не требуется. При первой сборке в проекте создаётся `.luac/android-id` — сохраняйте этот файл, чтобы следующие сборки имели тот же Android package ID. Копии проекта с одинаковым ID считаются одним приложением. Подписывающий ключ привязан к установленной Studio на устройстве и не входит в `.luac`: после её удаления или при сборке на другом устройстве Android может отказаться обновлять старый APK из-за другой подписи. Это не выпускной процесс для Google Play; используйте собственную управляемую подпись для публикации.

Standalone APK использует те же поддерживаемые функции embedded runtime и Canvas-визуализацию, что и запуск проекта в Studio. Общие ограничения архива (в том числе 8 МиБ на файл и 16 МиБ на весь проект) применяются и при сборке APK. Сейчас имя приложения в лаунчере — **LuaC Project**, без настройки иконки; это не компиляция Lua в машинный код.

### Nightly builds

Workflow **Nightly builds** проверяет изменения и публикует артефакты; также задан ежедневный запуск в 02:00 UTC. GitHub запускает расписание только из workflow на основной ветке, поэтому автоматический nightly заработает после попадания изменений в неё. Это сборка Studio и нативных инструментов, не APK пользовательского проекта.

### Пакеты примеров

Кнопка **Пакеты** на экране проектов открывает прокручиваемый каталог. Создание примера делает отдельную редактируемую копию: доступны многофайловый проект, JSON/хеши/Base64, сенсорная сцена и FFI. FFI требует совместимые системные `libffi` и C-библиотеку и может быть недоступен на Android; пример сообщает об этом в консоли. В примере сцены `scripts/luac_gui.lua` — небольшой Lua-модуль поверх существующего `engine` с прямоугольниками, текстом и касаниями. Android отображает сцену через Canvas: это не полноценный Love2D и не Vulkan-рендерер.

### Paint в Android Studio

В меню файлов проекта выберите **Paint — new image**. Доступны кисть с выбором цвета и толщины, ластик, отмена последнего действия и очистка. Кнопка **Save PNG** сохраняет прозрачный рисунок 512 × 512 в папку `assets` текущего проекта; существующие файлы не перезаписываются. Перед выходом из редактора несохранённый рисунок можно сохранить или продолжить редактирование. Это простой редактор новых изображений, а не редактор существующих PNG.

### Экспорт и импорт проектов Android

В списке проектов нажмите **Import .luac**, чтобы выбрать сохранённый проект; экспорт доступен через **⋮ → Export .luac** или меню файлов проекта. Формат `.luac` — UTF-8 JSON с `format: "luac-project"`, `version: 1`, именем проекта, списком `directories` и массивом `files` с относительными путями и Base64-данными. Он сохраняет бинарные файлы и пустые папки, а импорт создаёт отдельный проект, не перезаписывая существующий. Для защиты импорт ограничен 1024 записями, 8 МиБ на файл и 16 МиБ данных; небезопасные пути и символьные ссылки не поддерживаются. `.luac` — архив исходников, не исполняемый APK.

### Совместимость Lua: текущий этап

Встроенная функция `warn` поддерживает `@on`/`@off` (по умолчанию выключена), выводит предупреждения в stderr или в консоль встроенного проекта. В embedded project runtime динамический `require(name)` ищет Lua-файлы по `package.path` (по умолчанию `./?.lua;./?/init.lua`) и учитывает `package.preload` и `package.loaded`. Шаблоны относительно корня проекта; абсолютные пути и компоненты `..` отклоняются. Это лексическое ограничение, не защита от симлинков. Пути `package.cpath` и динамическая загрузка бинарных C-модулей не реализованы; desktop AOT/JIT по-прежнему связывает Lua-файлы с литеральными именами на этапе компиляции.

Во встроенном project runtime добавлены `load`, `loadstring`, `loadfile` и `dofile` для текстовых чанков. `load` принимает строку, возвращает функцию или `nil, ошибка`; `loadfile`/`dofile` читают файл. Чанки выполняются в глобальной среде проекта. Функция-reader, бинарные чанки, пользовательская среда и ввод из stdin пока не поддерживаются; в desktop AOT/JIT эти функции пока отсутствуют.

Бинарные форматы `string.pack`, `string.unpack` и `string.packsize` доступны в native runtime и embedded-проектах.

Локальные переменные с атрибутом `<const>` проверяются при компиляции и загрузке, включая присваивания из вложенных функций; изменять поля таблицы, на которую указывает константа, по-прежнему можно.

`<close>`/`__close`, финализаторы `__gc`, классический стековый Lua C API, полноценные userdata, `debug`, `string.dump` и расширенные типы/коллбэки/varargs FFI пока не реализованы. Это не полная совместимость с Lua 5.4 или LuaJIT.

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

Также доступны `luac_check`, `luac_version` и `luac_free`. При compile definition `LUAC_EMBEDDED_RUNTIME` добавляются `luac_project_open/step/touch/close` для многофайлового in-process runtime без C toolchain; `step` возвращает JSON scene/output. Тот же API используется Android JNI и может подключаться к C/C++-проектам. Regression test — `tests/embedded_test.c` и `tests/embedded_project/`.

## Копирование в собственный build

Lua/LuaJIT не скачиваются и не подключаются. Для compiler + всех native libraries скопируйте `src/luac.c`, `src/luac.h`, `src/runtime.h`, `src/luac_pack.h`, `src/luac_libraries.h`, `src/luac_tls.h`, `src/luac_audio.h` и `src/third_party/`; для embedded project runner также нужен `src/luac_interpreter.h`. Без playback miniaudio header не компилируется, но должен быть доступен в source distribution. Для C++ engine отдельно достаточно `src/engine.hpp`:

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
- `docs/LIBRARIES.md` — справочник по 197 native APIs;
- `CMakeLists.txt`, `cmake/` — build/install/export package и AOT CTest;
- `examples/embed/` — независимый consumer для CMake package;
- `gui/` — Nuklear editor, command→geometry adapter, desktop Vulkan renderer и GLSL shaders;
- `gui/third_party/` — pinned Nuklear single-header distribution и лицензия;
- `android/` — APK, Java UI, JNI и Android Vulkan renderer;
- `.github/workflows/build.yml` — тесты и кроссплатформенные артефакты;
- `examples/demo.lua` — пример;
- `tests/features.lua`, `tests/compat.lua` — проверка языка, runtime и стандартных библиотек.

Лицензия проекта находится в `LICENSE`.
