local object = setmetatable({value = 40}, {
  __add = function(self, other) return self.value + other end
})
print("Embedded native LuaC: " .. (object + 2))
