local greeting = require('scripts.greeting')
print(greeting.message('LuaC'))
world = engine.world()
engine.text(world, 'Hello from a multi-file project!', 18, 40, 20)
