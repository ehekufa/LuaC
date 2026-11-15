# Установка и первый запуск

## Готовая сборка

Откройте [Releases](https://github.com/ehekufa/LuaC/releases) или [Actions](https://github.com/ehekufa/LuaC/actions) и выберите сборку для своей платформы. Наличие и состав файлов зависят от конкретного релиза или запуска workflow. Для просмотра артефактов Actions может потребоваться вход в GitHub.

## Сборка из исходников

На Linux или macOS понадобятся `make` и C-компилятор с поддержкой GNU C99. На Windows можно использовать среду MinGW с `make` и GCC.

```sh
git clone https://github.com/ehekufa/LuaC.git
cd LuaC
make
./luac --version
```

На Windows имя исполняемого файла — `luac.exe`. Для JIT и AOT также нужен доступный C-компилятор; установите `CC` или укажите `--cc`, если его нужно выбрать явно.

## Первый запуск

```sh
./luac examples/demo.lua
```

По умолчанию LuaC генерирует C, компилирует его в разделяемую библиотеку и запускает полученный native-код. Несколько полезных команд:

```sh
./luac --check examples/demo.lua                   # проверить синтаксис
./luac --emit-c examples/demo.lua -o demo.c        # получить C
./luac --aot examples/demo.lua -o demo             # собрать программу
./demo
```

`--check` не выполняет программу. Сгенерированному C нужны `runtime.h` и соседние заголовки LuaC; это не полностью автономный C-файл. Для выбора компилятора используйте, например, `./luac --cc clang examples/demo.lua`.

## Примеры

В каталоге [examples](https://github.com/ehekufa/LuaC/tree/main/examples) есть примеры для данных, сети, аудио и сцены. Дополнительные зависимости и флаги сборки описаны в [README](https://github.com/ehekufa/LuaC/blob/main/README.md).
