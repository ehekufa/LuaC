-- This example needs a compatible system libffi and a loadable libc.
-- Android devices often do not expose libffi: the example reports that safely.
local demo = require('scripts.native')
local ok, message = pcall(demo.run)
if ok then print(message) else print('FFI unavailable:', message) end
world = engine.world()
engine.text(world, 'FFI result is in the console', 18, 40, 18)
