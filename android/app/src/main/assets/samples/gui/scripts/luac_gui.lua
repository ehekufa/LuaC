-- A small project-local wrapper around LuaC's existing retained scene API.
-- It is not Love2D and does not provide Vulkan rendering.
local gui = {}
function gui.rectangle(world, name, x, y, width, height, r, g, b)
  local entity = engine.create(world, name, x, y)
  engine.set_size(entity, width, height)
  engine.set_color(entity, r, g, b, 1)
  return entity
end
function gui.move(entity, x, y)
  entity.x = x
  entity.y = y
end
function gui.label(world, message, x, y, size)
  return engine.text(world, message, x, y, size)
end
function gui.touch(world, id)
  return engine.touch_pos(world, id)
end
return gui
