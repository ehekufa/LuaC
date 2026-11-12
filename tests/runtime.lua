-- Signed 64-bit integer values, not doubles limited to 53 bits.
local max, min = math.maxinteger, math.mininteger
assert(math.type(max) == 'integer' and math.type(1.0) == 'float')
assert(max == 9223372036854775807 and tostring(max) == '9223372036854775807')
assert(min == -9223372036854775808 and max + 1 == min)
assert((max & max) == max and (1 << 62) == 4611686018427387904)
assert(((1 << 62) << 1) == min and (~0) == -1)
assert(7 // 2 == 3 and -7 // 2 == -4 and -7 % 2 == 1)
assert(1 == 1.0 and math.tointeger(max) == max)
local keys = {[max] = 'max', [1] = 'one'}
assert(keys[max] == 'max' and keys[1.0] == 'one')

-- Stackful yields retain generated native locals across nested calls.
local co = coroutine.create(function(a, b)
  local alive = 'yes'
  local x, y = coroutine.yield('first', a + b)
  assert(x == 'x' and y == 9)
  local function nested(v)
    local twice = v * 2
    assert(coroutine.yield('nested', twice) == 'back')
    return twice + 1
  end
  local result = nested(5)
  assert(alive == 'yes')
  return 'done', result
end)
local ok, text, value = coroutine.resume(co, 3, 4)
assert(ok and text == 'first' and value == 7)
ok, text, value = coroutine.resume(co, 'x', 9)
assert(ok and text == 'nested' and value == 10)
ok, text, value = coroutine.resume(co, 'back')
assert(ok and text == 'done' and value == 11 and coroutine.status(co) == 'dead')
ok, text = coroutine.resume(co)
assert(not ok and text:find('dead'))
local wrapped = coroutine.wrap(function() coroutine.yield(10) return 20 end)
assert(wrapped() == 10 and wrapped() == 20)
local bad = coroutine.create(function() error('expected coroutine failure') end)
ok, text = coroutine.resume(bad)
assert(not ok and text:find('expected coroutine failure'))
assert(not coroutine.isyieldable())

-- Static disk modules are linked into both JIT and AOT programs.
local module = require('modules.answer')
assert(module.answer() == 'answer=42')
assert(require('modules.answer') == module)

-- Cyclic ownership graphs and weak table entries are collected while running.
local before = collectgarbage('count')
for i = 1, 100 do local a, b = {}, {}; a.peer = b; b.peer = a end
collectgarbage('collect')
assert(collectgarbage('count') < before + 100)
local weak_keys = setmetatable({}, {__mode = 'k'})
local weak_values = setmetatable({}, {__mode = 'v'})
local key, cached = {}, {}
weak_keys[key] = 'cached'; weak_keys[true] = 'entry'; weak_values[1] = cached
key, cached = nil, nil
collectgarbage('collect')
local nk, nv = 0, 0
for k, v in pairs(weak_keys) do nk = nk + 1 end
for k, v in pairs(weak_values) do nv = nv + 1 end
assert(nk == 1 and weak_keys[true] == 'entry' and nv == 0)
local survivor = {}
local weak = setmetatable({}, {__mode = 'kv'})
weak[survivor] = survivor
collectgarbage('collect')
assert(weak[survivor] == survivor and collectgarbage('isrunning'))
print('LuaC runtime roadmap: OK')
