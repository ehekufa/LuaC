local object = setmetatable({value = 40}, {
  __add = function(self, other) return self.value + other end
})
assert(require("json").encode({ok = true}) == '{"ok":true}')
assert(require("hash").sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
print("Embedded native LuaC: " .. (object + 2))
