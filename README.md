# LuaC

**LuaC (Lua Compiler)** — небольшой самостоятельный компилятор программ на Lua-подобном языке в машинный код. Исходные файлы используют обычное расширение **`.lua`**. Код оригинального Lua, его VM и интерпретатор не используются.

Есть два пути выполнения одного и того же сгенерированного C-кода:

- **JIT** — во время запуска создаётся разделяемая библиотека, загружается через `dlopen`, после чего управление получает native-код;
- **AOT** — создаётся обычный самостоятельный исполняемый файл.

Это не транслятор в исходник Lua и не обёртка над `liblua`.

## Быстрый старт

Нужны компилятор C с поддержкой GNU C99 и POSIX-система (`dlopen`, `mkdtemp`). Зависимостей от Lua нет.

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
```

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
- `if / elseif / else`, `while`, `repeat`, `break`;
- числовой и generic `for`;
- функции, рекурсия, анонимные функции, методы `:`, замыкания и изменяемые upvalues;
- varargs `...` и множественные возвращаемые значения;
- таблицы, array/named/keyed-поля, `pairs`, `ipairs`, `next`;
- `pcall`, `assert`, `error`, `select`, `type`, `tostring`, `tonumber`;
- базовые модули `math`, `string`, `table`, а также `io.write`, `os.clock`, `os.time`.

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

Это работающее ядро компилятора, **пока не полная drop-in реализация Lua 5.4**. Метатаблицы, coroutines, модули `require`, `load`/`dofile`, `goto`, целочисленный подтип и bitwise-операторы ещё не реализованы. Стандартная библиотека покрыта частично. Эти ограничения указаны явно: LuaC не претендует на запуск любого существующего Lua-проекта без изменений.

## Готовые сборки через GitHub Actions

Workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) запускается на `push`, `pull_request` и вручную через **Actions → Native builds → Run workflow**. После успешного прогона артефакты доступны на странице workflow:

| Артефакт | Содержимое |
|---|---|
| `LuaC-windows-x86_64` | `luac.exe`, статическая и динамическая библиотеки, headers |
| `LuaC-linux-x86_64` | CLI, `libluac.a`, `libluac.so`, headers |
| `LuaC-macos-arm64` | CLI, `libluac.a`, `libluac.dylib`, headers |
| `LuaC-Studio-windows-x86_64` | Vulkan GUI `.exe`, shaders, GLFW DLL |
| `LuaC-Studio-linux-x86_64` | Vulkan GUI, CLI, shaders |
| `LuaC-Studio-Android-APK` | устанавливаемый debug APK с Java-редактором и Vulkan Surface |

Для JIT/AOT на desktop нужен системный C-компилятор (`cc`, GCC или Clang). В APK синтаксическая проверка и генерация C выполняются внутри нативной библиотеки; запуск JIT на Android не требуется и не выполняется, поскольку Android не поставляется с host C toolchain.

## LuaC Studio — Vulkan GUI

Desktop-редактор написан на C++17 без ImGui: GLFW создаёт окно, а собственный Vulkan renderer выводит swapchain, graphics pipeline, цветные панели и bitmap-font.

```sh
# Debian/Ubuntu
sudo apt install libglfw3-dev libvulkan-dev glslang-tools pkg-config
make gui
./build/luac-gui examples/demo.lua
```

Горячие клавиши:

- **F5** — сохранить при необходимости и запустить через JIT;
- **F6** — проверить синтаксис;
- **Ctrl+S** — сохранить;
- мышь и клавиатура — редактирование исходника.

Пути можно переопределить переменными `LUAC_COMPILER`, `LUAC_SHADER_DIR` и `LUAC_RUNTIME`. В пакетах shaders находятся в `build/`, поэтому GUI следует запускать из корня распакованного пакета.

Android-версия находится в `android/` и содержит редактор, проверку, просмотр сгенерированного C и нативную Vulkan-очистку Surface. Сборка из workflow использует Android SDK 35, NDK 27 и CMake.

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

## Тесты

```sh
make test
SANITIZE=1 make test  # дополнительно ASan/UBSan, если компилятор их поддерживает
```

Тесты прогоняют один набор программ через JIT и AOT, проверяют генерацию C, диагностику неправильного кода, замыкания, множественные значения, таблицы, циклы и обработку ошибок.

## Устройство проекта

- `src/luac.c` — lexer, parser, AST, генератор C, JIT-loader, C API и CLI;
- `src/luac.h` — публичный API библиотеки;
- `src/runtime.h` — значения, таблицы, функции, ownership runtime и стандартная библиотека;
- `gui/` — desktop Vulkan renderer, редактор и GLSL shaders;
- `android/` — APK, Java UI, JNI и Android Vulkan renderer;
- `.github/workflows/build.yml` — тесты и кроссплатформенные артефакты;
- `examples/demo.lua` — пример;
- `tests/features.lua` — проверка языка и runtime.

Лицензия проекта находится в `LICENSE`.
