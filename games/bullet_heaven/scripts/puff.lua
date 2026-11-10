-- Short-lived death particle; fades out with a tween then removes itself.
local Puff = { type = "sprite", props = { w = 7, h = 7, z = 8 } }

function Puff:ready()
  local a = math.random() * math.pi * 2
  local s = aether.math.random_range(40, 140)
  self.dx, self.dy = math.cos(a) * s, math.sin(a) * s
  aether.tween(self, { opacity = 0, scale = 0.2 }, 0.4, {
    ease = aether.ease.out_quad,
    on_done = function(n) n:destroy() end,
  })
end

function Puff:update(dt)
  self.x = self.x + self.dx * dt
  self.y = self.y + self.dy * dt
end

return Puff
