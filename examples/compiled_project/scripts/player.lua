local player = {}
function player.create(world)
  local entity = engine.create(world, 'Player', 0, 0)
  engine.set_size(entity, 24, 32)
  return entity
end
function player.move(entity, dt)
  engine.translate(entity, dt * 10, 0)
end
return player
