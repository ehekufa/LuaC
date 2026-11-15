# Studio и проекты

## Desktop Studio

LuaC Studio — редактор с Vulkan и GLFW. Для сборки из исходников на Debian/Ubuntu установите зависимости и выполните:

```sh
sudo apt install libglfw3-dev libvulkan-dev glslang-tools pkg-config
make gui
./build/luac-gui examples/demo.lua
```

**F5** запускает исходник, **F6** проверяет синтаксис, **F7** переключает Editor и Engine, **Ctrl+S** сохраняет файл. Для запуска из редактора нужен доступный `luac` и C-компилятор. Возможности интерфейса и отображение символов зависят от версии сборки.

## Android Studio

Android-приложение открывает список проектов. Создайте проект, откройте `main.lua`, измените код и нажмите **Run**. Вывод `print` и ошибки показываются в консоли; **Stop / Edit** останавливает проект.

Пример организации проекта:

```text
MyProject/
├── main.lua
├── scripts/
│   └── player.lua
└── assets/
    └── image.png
```

`main.lua` — точка входа. Подключайте модули, например, через `require('scripts.player')`; изображения PNG/JPEG можно импортировать в `assets`. После переименования файлов исправьте ссылки и `require` вручную.

**Run** на Android действительно выполняет проект через встроенный AST-исполнитель поверх LuaC runtime, а не компилирует его в машинный код на телефоне. **Check** только проверяет синтаксис. Минимальная версия Android — 8 / API 26.

Для `world = engine.world()` и `function update(dt)` runner обновляет игровую сцену по кадрам. Это собственный движок LuaC, а не полноценный LÖVE.
