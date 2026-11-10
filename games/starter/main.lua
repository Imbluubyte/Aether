-- Starter project: move the square with WASD / arrow keys / the on-screen stick.
local Game = {}

function Game:ready()
  aether.input.bind("move_left", "key:a", "key:left", "axis:leftx-")
  aether.input.bind("move_right", "key:d", "key:right", "axis:leftx+")
  aether.input.bind("move_up", "key:w", "key:up", "axis:lefty-")
  aether.input.bind("move_down", "key:s", "key:down", "axis:lefty+")
  aether.input.virtual_stick("move")
  self.hero = aether.node.create("sprite", { w = 48, h = 48, color = "cyan" })
  self.title = aether.node.create("label", { text = "HELLO AETHER", x = 24, y = 24, text_size = 4, screen_space = true })
end

function Game:update(dt)
  local mx, my = aether.input.vector("move")
  self.hero.x = self.hero.x + mx * 300 * dt
  self.hero.y = self.hero.y + my * 300 * dt
end

return Game
