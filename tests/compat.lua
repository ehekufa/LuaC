local function eq(actual, expected, label)
  if actual ~= expected then
    error((label or "value") .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual))
  end
end

-- Native Lua 5.3/LuaJIT-style bitwise operators, with no VM or bytecode.
eq(0x0f & 0x33, 3, "band")
eq(0x0f | 0x30, 63, "bor")
eq(0x0f ~ 0x03, 12, "bxor")
eq(~0, -1, "bnot")
eq(1 << 4, 16, "shl")
eq(-16 >> 2, -4, "shr")
eq(1 << 2 + 1, 8, "shift precedence")
eq(0xf0 | 0x0f & 0x03, 243, "and before or")

local visited = {}
for i = 1, 5 do
  if i % 2 == 0 then goto continue end
  visited[#visited + 1] = i
  ::continue::
end
eq(table.concat(visited, ","), "1,3,5", "goto")

-- Metatables and raw access are implemented by the native ownership runtime.
local prototype = {greet = function(self) return "hello " .. self.name end}
local object = setmetatable({name = "LuaC"}, {__index = prototype})
eq(object:greet(), "hello LuaC", "__index")
local backing = {}
local writes = setmetatable({}, {__newindex = backing})
writes.answer = 42
eq(rawget(writes, "answer"), nil, "rawget bypass")
eq(rawget(backing, "answer"), 42, "__newindex")
rawset(writes, "answer", 7)
eq(rawget(writes, "answer"), 7, "rawset")
eq(rawequal(writes, writes), true, "rawequal")
eq(rawlen({1, 2, 3}), 3, "rawlen")

local callable = setmetatable({base = 10}, {__call = function(self, n) return self.base + n end})
eq(callable(5), 15, "__call")
local number = setmetatable({value = 8}, {
  __add = function(self, other) return self.value + other end,
  __len = function(self) return self.value end,
  __tostring = function(self) return "number:" .. self.value end
})
eq(number + 2, 10, "__add")
eq(#number, 8, "__len")
eq(tostring(number), "number:8", "__tostring")
eq(_G.rawget == rawget, true, "_G")
eq(tonumber("ff", 16), 255, "tonumber base")
eq(tonumber("-101", 2), -5, "tonumber binary")
local handled, detail = xpcall(function() error("boom") end, function(message) return "handled:" .. message end)
eq(handled, false, "xpcall status")
eq(detail, "handled:boom", "xpcall handler")
eq(collectgarbage("isrunning"), false, "no tracing collector")
eq(type(collectgarbage("count")), "number", "memory count")

-- LuaJIT bit API and Lua 5.2 bit32 helpers.
eq(bit.band(0xff, 0x0f), 15, "bit.band")
eq(bit.bxor(0xff, 0x0f), 240, "bit.bxor")
eq(bit.lshift(1, 8), 256, "bit.lshift")
eq(bit.rshift(0x80000000, 31), 1, "logical right shift")
eq(bit.arshift(0x80000000, 31), -1, "arithmetic right shift")
eq(bit.rol(0x12345678, 8), 0x34567812, "bit.rol")
eq(bit.ror(0x12345678, 8), 0x78123456, "bit.ror")
eq(bit.bswap(0x12345678), 0x78563412, "bit.bswap")
eq(bit.tohex(0x2a), "0000002A", "bit.tohex")
eq(bit32.extract(0xabcd, 4, 8), 0xbc, "bit32.extract")
eq(bit32.replace(0, 0xf, 4, 4), 0xf0, "bit32.replace")
eq(bit32.btest(0x10, 0x01), false, "bit32.btest")

local packed = table.pack("a", "b", "c")
eq(packed.n, 3, "table.pack")
eq(select("#", table.unpack(packed)), 3, "table.unpack count")
local moved = table.move({1, 2, 3, 4}, 2, 4, 1)
eq(table.concat(moved, ","), "2,3,4,4", "table.move overlap")
local sorted = {5, 1, 4, 2, 3}
table.sort(sorted)
eq(table.concat(sorted, ","), "1,2,3,4,5", "table.sort")
table.sort(sorted, function(a, b) return a > b end)
eq(table.concat(sorted, ","), "5,4,3,2,1", "table.sort comparator")
eq(math.fmod(7, 4), 3, "math.fmod")
eq(math.ldexp(0.5, 4), 8, "math.ldexp")
eq(math.tointeger(4.0), 4, "math.tointeger")
eq(math.type(4.0), "integer", "math.type")
eq(math.log(8, 2), 3, "math.log base")
local integral, fraction = math.modf(3.25)
eq(integral, 3); eq(fraction, 0.25)

eq(select("#", string.byte("ABC", 1, 3)), 3, "string.byte results")
eq(string.byte("A"), 65, "string.byte")
eq(string.char(65, 66, 67), "ABC", "string.char")
eq(string.reverse("LuaC"), "CauL", "string.reverse")
eq(string.rep("x", 3, "-"), "x-x-x", "string.rep separator")
eq(("Abc"):upper(), "ABC", "string method")
local first, last, key, value = string.find("key = 42", "(%a+)%s*=%s*(%d+)")
eq(first, 1); eq(last, 8); eq(key, "key"); eq(value, "42")
local plainStart, plainEnd = string.find("a.b", ".", 1, true)
eq(plainStart, 2); eq(plainEnd, 2)
eq(string.match("abba", "(%a)(%a)%2%1"), "a", "capture backreference")
local digits = {}
for digitsText in string.gmatch("a12b3c45", "%d+") do digits[#digits + 1] = digitsText end
eq(table.concat(digits, ","), "12,3,45", "gmatch")
local substituted, replacements = string.gsub("a=1 b=22", "(%a+)=(%d+)", "%2=%1")
eq(substituted, "1=a 22=b"); eq(replacements, 2)
eq(string.gsub("one two", "%a+", function(word) return "[" .. word .. "]" end), "[one] [two]")
eq(string.gsub("a b c", "%a", {a = "A", c = "C"}), "A b C")
eq(string.match("call(a(b)c)", "%b()"), "(a(b)c)", "balanced pattern")
eq(string.match("123 hello world", "%f[%a]%a+"), "hello", "frontier pattern")

local unicode = utf8.char(0x41, 0x423, 0x1f600)
eq(utf8.len(unicode), 3, "utf8.len")
eq(select("#", utf8.codepoint(unicode, 1, -1)), 3, "utf8.codepoint results")
local cp1, cp2, cp3 = utf8.codepoint(unicode, 1, -1)
eq(cp1, 0x41); eq(cp2, 0x423); eq(cp3, 0x1f600)
eq(utf8.offset(unicode, 2), 2, "utf8.offset")
local codepoints = {}
for bytePosition, codepoint in utf8.codes(unicode) do codepoints[#codepoints + 1] = codepoint end
eq(table.concat(codepoints, ","), tostring(0x41) .. "," .. tostring(0x423) .. "," .. tostring(0x1f600), "utf8.codes")

local ioPath = "tests/.tmp/luac-io.txt"
local file = assert(io.open(ioPath, "w"))
eq(io.type(file), "file", "io.type")
file:write("abc\n", 42, "\nxyz")
eq(file:close(), true, "file:close")
eq(io.type(file), "closed file", "closed file type")
file = assert(io.open(ioPath, "r"))
eq(file:read("*l"), "abc", "file read line")
eq(file:read("*n"), 42, "file read number")
eq(file:read("*a"), "\nxyz", "file read all")
eq(file:seek("set", 0), 0, "file seek")
eq(file:read("*L"), "abc\n", "file read line with newline")
file:close()
local lines = {}
for line in io.lines(ioPath) do lines[#lines + 1] = line end
eq(table.concat(lines, ","), "abc,42,xyz", "io.lines")
os.remove(ioPath)
local epoch = os.date("!*t", 0)
eq(epoch.year, 1970); eq(epoch.month, 1); eq(epoch.day, 1)
eq(type(os.time(epoch)), "number", "os.time table")
eq(os.difftime(20, 5), 15, "os.difftime")
eq(type(os.getenv("PATH")), "string", "os.getenv")
eq(type(os.tmpname()), "string", "os.tmpname")
eq(string.format("%s:%04d:%.2f", "value", 7, 1.25), "value:0007:1.25", "string.format")
eq(string.format("%x", 255), "ff", "string.format hex")
eq(string.format("%q", "a\nb"), '"a\\nb"', "string.format quote")
local loads = 0
package.preload.native_module = function(name)
  loads = loads + 1
  return {name = name, answer = 42}
end
local module = require("native_module")
eq(module.answer, 42, "require preload")
eq(require("native_module") == module, true, "package.loaded cache")
eq(loads, 1, "module loaded once")

print("LuaC compatibility: OK")
