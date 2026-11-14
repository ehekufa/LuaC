local scene = {x = 200, y = 200, elapsed = 0}
function scene.update(dt)
  scene.elapsed = scene.elapsed + dt
end
return scene
