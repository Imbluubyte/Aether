-- Minimal platformer (also used as the guide's platformer example).
local Game = {}

function Game:ready()
  aether.physics.set_layers({ "player", "ground" })
  aether.physics.set_gravity(0, 1400)
  aether.input.bind("left", "key:a", "key:left", "axis:leftx-")
  aether.input.bind("right", "key:d", "key:right", "axis:leftx+")
  aether.input.bind("jump", "key:space", "pad:south")

  -- Level: a floor and two platforms. Static bodies are solid for dynamic bodies whose mask includes "ground".
  for _, r in ipairs({ { 0, 400, 1600, 40 }, { 300, 300, 160, 20 }, { 560, 220, 160, 20 } }) do
    aether.node.create("body", { x = r[1], y = r[2], w = r[3], h = r[4], color = "gray", layer = "ground", body_type = "static" })
  end

  self.player = aether.spawn("player.lua", { x = 0, y = 350 })
  aether.camera.follow(self.player, 6)
end

return Game
