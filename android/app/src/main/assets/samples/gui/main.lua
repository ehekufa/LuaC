local gui = require('scripts.luac_gui')
world = engine.world()
local box = gui.rectangle(world, 'Box', 0, 0, 64, 64, 0.2, 0.7, 0.9)
gui.label(world, 'Touch to move the square', 18, 40, 18)
function update(dt)
  local x, y = gui.touch(world, 0)
  if x then gui.move(box, x, y) end
end
print('luac_gui Canvas example ready')
