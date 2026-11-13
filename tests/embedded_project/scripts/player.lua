local player = {}
function player.create(world, texture)
  local entity = engine.create(world, 'Player', 0, 0)
  engine.set_sprite(entity, texture, 64, 64)
  engine.set_velocity(entity, 10, 0)
  return entity
end
function player.update(entity, dt)
  engine.translate(entity, 10 * dt, 0)
end
return player
