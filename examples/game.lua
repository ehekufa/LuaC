-- Native scene simulation. Works with JIT and AOT; Vulkan is not required.
local world = engine.world()
local player = engine.create(world, "Player", 0, 0)
local goal = engine.create(world, "Goal", 60, 0)
engine.set_size(player, 20, 20)
engine.set_size(goal, 30, 30)
engine.set_velocity(player, 30, 0)
engine.add_component(player, "health", 100)
engine.add_tag(player, "player")

for frame = 1, 120 do
  engine.update(world, 1 / 60)
  if #engine.collisions(world, player) > 0 then
    print("Goal reached at " .. string.format("%.2f", world.time) .. "s")
    break
  end
end

print("Entities: " .. engine.count(world) .. ", player x=" .. string.format("%.2f", player.x))
