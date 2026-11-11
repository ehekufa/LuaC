# LuaC 0.4 native libraries

Все перечисленные функции реализованы в `src/luac_libraries.h` и компилируются в тот же native C executable/shared library, что и программа. Lua VM, динамический plugin loader и внешняя `liblua` не используются.

Модули доступны двумя способами:

```lua
local encoded = json.encode({answer = 42})
local sameJson = require("json") -- встроенный module, уже находится в package.loaded
assert(sameJson == json)
```

Новые 13 модулей содержат **151 callable API** (без подсчёта alias `net`). Существующие `string`, `table`, `math`, `bit`, `bit32`, `utf8`, `io` и `os` также остаются доступны.

## Data and text

| Module | Functions |
|---|---|
| `text` | `trim`, `ltrim`, `rtrim`, `reverse`, `capitalize`, `title`, `swapcase`, `startswith`, `endswith`, `contains`, `find`, `count`, `split`, `join`, `replace`, `lpad`, `rpad`, `center`, `repeat` |
| `hex` | `encode`, `decode`; encode принимает optional `"upper"` |
| `base64` | `encode`, `decode`; decoder принимает стандартный и URL-safe alphabet, игнорирует whitespace |
| `url` | `encode`, `decode`, `queryencode`, `querydecode`, `parse`, `build` |
| `json` | `encode`, `decode`; nested arrays/objects, escapes, UTF-16 surrogate pairs, ограничение глубины |
| `hash` | `crc32`, `adler32`, `fnv1a`, `djb2`, `sha256`, `sha256raw`, `hmac_sha256` |

`json.decode` возвращает `nil, message` для malformed input. JSON `null` отображается в Lua `nil`; как и в обычной Lua table, array с `null` в середине не сохраняет hole как отдельное значение. CRC/Adler/FNV/DJB2 предназначены для checksums, не для password hashing. SHA-256 и HMAC реализованы непосредственно в LuaC.

```lua
local body = json.encode({name = "LuaC", values = {1, 2, 3}})
local decoded, err = json.decode(body)
assert(decoded and decoded.values[3] == 3)
assert(hash.sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
```

## Filesystem, paths and time

| Module | Functions |
|---|---|
| `fs` | `stat`, `exists`, `isfile`, `isdir`, `size`, `modified`, `list`/`listdir`, `mkdir`, `mkdirs`, `rmdir`, `chdir`, `getcwd`, `remove`, `rename`, `readfile`, `writefile`, `appendfile`, `copyfile`, `touch` |
| `path` | `basename`, `dirname`, `extension`/`ext`, `stem`, `join`, `isabsolute`, `normalize`, `absolute` |
| `time` | `now`, `monotonic`, `ticks`, `millis`, `sleep`, `iso`, `localiso`, `parts`, `localparts` |

`readfile`/`writefile` работают в binary mode и сохраняют нулевые байты. `path.normalize` — лексическая операция, не разрешает symlinks. `time.monotonic` не зависит от перевода системных часов.

## Vector math

`vec`: `add`, `sub`, `mul`, `dot`, `cross`, `distance`, `distance2`, `lerp`, `length`, `length2`, `normalize`, `scale`, `perpendicular`, `angle`, `rotate`, `clamp`, `reflect`.

Вектор представлен двумя return values:

```lua
local x, y = vec.normalize(3, 4) -- 0.6, 0.8
local rx, ry = vec.rotate(10, 0, math.pi / 2)
```

## TCP, UDP and DNS

`socket` (и alias `net`):

- constructors: `tcp()`, `udp()`;
- convenience: `connect(host, port)`, `bind(host, port)` (TCP listener), `select(read, write, timeout)`;
- DNS: `resolve(host)`, `addresses(host)`, `gethostname()`;
- methods: `connect`, `bind`, `listen`, `accept`, `send`, `receive`/`recv`, `sendto`, `receivefrom`/`recvfrom`, `close`, `settimeout`, `setoption`, `getsockname`, `getpeername`, `shutdown`.

`receive` принимает byte count, `"*a"` или `"*l"`. `setoption` поддерживает `nonblocking`, `reuseaddr`, `reuseport` (если ОС предоставляет), `broadcast`, `keepalive`, `nodelay`. Socket objects являются ownership-managed userdata: последний release или shutdown контекста автоматически закрывает descriptor; на Windows Winsock инициализируется и освобождается вместе с контекстом.

```lua
local server = socket.bind("127.0.0.1", 8080)
local client = socket.connect("127.0.0.1", 8080)
client:send("hello")
local peer = server:accept()
assert(peer:receive(5) == "hello")
peer:close(); client:close(); server:close()
```

## HTTP

- `http.get(url)`;
- `http.post(url, body, content_type)`;
- `http.request(url, method, body, content_type)`.

Возврат: `body, status, headers`. Header names приводятся к lowercase. Поддерживаются Content-Length/close-delimited responses и chunked transfer decoding. **TLS backend пока нет**: `https://` возвращает `nil, message`, а не небезопасную имитацию HTTPS. Для production HTTPS нужен отдельно проверенный TLS backend.

## Lua scene engine

`engine` работает с обычными ownership-managed Lua tables:

- world: `world`, `clear`, `count`, `list`, `ids`, `find`, `destroy`;
- entities: `create`/`entity`, `set_position`, `set_velocity`, `set_size`, `translate`, `set_rotation`, `rotate`, `set_scale`, `set_angular_velocity`, `set_active`, `set_color`;
- simulation: `update`, `pause`, `resume`, `bounds`, `intersects`, `collisions`;
- data: `add_component`, `get_component`, `remove_component`, `add_tag`, `remove_tag`, `has_tag`;
- camera: `camera`, `world_to_screen`, `screen_to_world`.

```lua
local world = engine.world()
local player = engine.create(world, "Player", 0, 0)
engine.set_velocity(player, 20, 0)
engine.add_component(player, "health", 100)
engine.add_tag(player, "player")
engine.update(world, 1 / 60)
```

## C++ engine embedding

`src/engine.hpp` — самостоятельный C++17 header-only API, не зависящий от Lua runtime, Vulkan или Studio:

```cmake
find_package(LuaC 0.4 CONFIG REQUIRED)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE LuaC::engine)
```

```cpp
#include <engine.hpp>
luac::engine::World world;
auto &player = world.create("Player", {0, 0});
player.body.velocity = {20, 0};
world.advance(1.0 / 60.0); // deterministic fixed steps
```

`World` включает stable entity IDs, transforms, bodies, sprites, tags, numeric components, camera, AABB collisions, play/pause/step и bounded fixed-step catch-up. Это расширяемый native 2D core; physics solver, audio, asset importer и networking для игровых объектов не выдаются за уже готовые подсистемы.
