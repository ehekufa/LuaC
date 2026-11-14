local player = require('scripts.player')
world = engine.world()
hero = player.create(world)
function update(dt)
  player.move(hero, dt)
end
print('Compiled project ready')
