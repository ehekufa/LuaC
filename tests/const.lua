local answer <const> = 42
local box <const>, variable = { value = answer }, 4
box.value = 43
variable = variable + 1
assert(answer == 42 and box.value == 43 and variable == 5)
local function read() return answer end
assert(read() == 42)
do
  local answer = answer + 1
  answer = answer + 1
  assert(answer == 44)
end
local function shadow(answer)
  answer = answer + 1
  return answer
end
assert(shadow(9) == 10)
for answer = 1, 2 do assert(answer > 0) end
repeat
  local done <const> = true
until done
local first, second <const> = 1, 2
first = first + second
assert(first == 3)
print('LuaC const: OK')
