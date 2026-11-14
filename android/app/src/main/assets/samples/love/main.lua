local scene = require('scripts.scene')
function love.load()
  love.graphics.setBackgroundColor(0.04, 0.08, 0.12)
  print('LÖVE-style example ready')
end
function love.update(dt)
  scene.update(dt)
end
function love.touchpressed(id, x, y)
  scene.x, scene.y = x, y
end
function love.touchmoved(id, x, y)
  scene.x, scene.y = x, y
end
function love.draw()
  love.graphics.setColor(0.2, 0.8, 0.65)
  love.graphics.rectangle('fill', scene.x - 35, scene.y - 35, 70, 70)
  love.graphics.setColor(1, 1, 1)
  love.graphics.circle('line', scene.x, scene.y, 45)
  love.graphics.print('Touch to move the square', 30, 30)
end
