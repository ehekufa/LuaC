-- LuaC: ordinary .lua source, compiled directly to native code.
local function fib(n)
  if n < 2 then
    return n
  end
  return fib(n - 1) + fib(n - 2)
end

local function counter(start)
  local value = start
  return function(step)
    value = value + step
    return value
  end
end

local numbers = {1, 2, 3, 4, 5}
local sum = 0
for i, value in ipairs(numbers) do
  sum = sum + value
end

local next = counter(10)
print("fib(10) =", fib(10))
print("sum =", sum)
print("closure =", next(5), next(2))
print("native =", _VERSION)
