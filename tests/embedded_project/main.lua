-- Dynamic names and user preloads are supported by the embedded runner.
package.preload['virtual'] = function(name) return { name = name } end
package.path = 'absent/?.lua;custom/?.lua;./?.lua;./?/init.lua'
local diskName = 'dy' .. 'namic'
assert(require(diskName) == 73)
assert(require('custom.nothing') == true)
assert(require('custom.nothing') == true)
local dynamicName = 'vir' .. 'tual'
assert(require(dynamicName).name == 'virtual')
assert(not pcall(require, 'missing.module'))
assert(not pcall(require, 'missing.module')) -- failure must not be cached
warn('@on')
warn('Embedded ', 'warning')
warn('@off')
require('settings')
local player = require('scripts.player')
world = engine.world()
engine.set_gravity(world, 0, 100)
local texture, textureError = engine.load_texture('assets/luac_logo.png')
assert(texture, textureError)
hero = player.create(world, texture)
engine.text(world, projectName, 10, 20, 18)
function update(dt)
  player.update(hero, dt)
end
print('Project started:', projectName)
