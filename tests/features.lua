local function eq(actual, expected, label)
  if actual ~= expected then
    error((label or "value") .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual))
  end
end

-- Numbers, precedence and logical operators return their operands.
eq(2 + 3 * 4, 14, "precedence")
eq(2 ^ 3 ^ 2, 512, "right associative power")
eq(7 // 2, 3, "floor division")
eq(7 % 3, 1, "modulo")
eq("a" .. 1 .. "b", "a1b", "concat")
eq(false or 42, 42, "or")
eq(true and 42, 42, "and")
eq(not nil, true, "not")

local a, b, c = 1, 2
eq(a, 1); eq(b, 2); eq(c, nil)
do
  local a = a + 10 -- the initializer sees the outer variable
  eq(a, 11, "local scope")
end
repeat
  local repeated = 9
until repeated == 9
local call_values = {global_placeholder and global_placeholder() or 4, (function() return 8 end)()}
eq(call_values[2], 8, "function calls in constructors")
a, b = b, a
eq(a, 2); eq(b, 1)

local total = 0
for i = 1, 10, 2 do total = total + i end
eq(total, 25, "numeric for")
local i = 0
while i < 5 do i = i + 1; if i == 3 then break end end
eq(i, 3, "while/break")
repeat i = i - 1 until i == 0
eq(i, 0, "repeat")

if false then error("bad if")
elseif 2 > 1 then eq(true, true)
else error("bad elseif") end

local function multi(a, b)
  return a + b, a - b, a * b
end
local x, y, z = multi(8, 3)
eq(x, 11); eq(y, 5); eq(z, 24)
eq(select("#", multi(1, 2)), 3, "multiple results")
local function expanded_return() return 99, multi(2, 1) end
local r1, r2, r3, r4 = expanded_return()
eq(r1, 99); eq(r2, 3); eq(r3, 1); eq(r4, 2)

local function pack(...)
  return select("#", ...), {...}
end
local count, args = pack("a", "b", "c")
eq(count, 3, "varargs")
eq(args[2], "b")
eq(#args, 3)

local t = {10, 20, name = "LuaC", [7] = 70}
eq(t[1], 10); eq(t.name, "LuaC"); eq(t[7], 70)
t.extra = "ok"
eq(t.extra, "ok")
t.extra = nil
eq(t.extra, nil)
table.insert(t, 30)
eq(t[3], 30)
eq(table.remove(t, 3), 30)
eq(table.concat({"a", "b", "c"}, "-"), "a-b-c")

local seen = 0
for k, v in pairs({a = 1, b = 2, c = 3}) do seen = seen + v end
eq(seen, 6, "pairs")

local function outer(start)
  local n = start
  return function(d) n = n + d; return n end
end
local counter = outer(5)
eq(counter(2), 7); eq(counter(3), 10)

local object = {value = 4}
function object:add(n) self.value = self.value + n; return self.value end
eq(object:add(6), 10, "method")

function global_twice(n) return n * 2 end
eq(global_twice(9), 18)
eq(string.upper("Abc"), "ABC")
eq(string.sub("abcdef", 2, 4), "bcd")
eq(string.rep("x", 3), "xxx")
eq(math.floor(3.9), 3)
eq(math.max(2, 8, 4), 8)

local ok, message = pcall(function() error("expected") end)
eq(ok, false)
eq(message, "expected")

-- Ownership cleanup: acyclic values die immediately; this cycle is reclaimed
-- by the context's shutdown safety net without running a tracing GC.
do
  local cycle = {name = "cycle"}
  cycle.self = cycle
end
for i = 1, 1000 do
  local temporary = {i, i + 1, i + 2}
  temporary = nil
end

print("LuaC tests: OK")
