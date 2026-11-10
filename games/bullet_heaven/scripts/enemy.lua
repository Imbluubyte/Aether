-- Reusable enemy. Spawn many with different stats:
--   aether.spawn("scripts/enemy.lua", { x = 0, y = 0, hp = 5, speed = 60, color = "purple" })
local State = require("scripts.state")

local Enemy = {
  type = "body",
  hp = 3,
  speed = 85,
  score = 10,
  flash = 0,
  props = { w = 26, h = 26, body_type = "kinematic", layer = "enemy", mask = "none", groups = { "enemies" }, z = 3 },
}

function Enemy:ready()
  self.base_color = self.color
end

function Enemy:update(dt)
  local player = aether.nearest_in_group("player", self.x, self.y)
  if player and not State.over then
    local dx, dy = self:direction_to(player)
    self.vx, self.vy = dx * self.speed, dy * self.speed
  else
    self.vx, self.vy = 0, 0
  end
  if self.flash > 0 then
    self.flash = self.flash - dt
    if self.flash <= 0 then self.color = self.base_color end
  end
end

function Enemy:take_damage(amount)
  self.hp = self.hp - amount
  self.color = "white"
  self.flash = 0.08
  if self.hp <= 0 and self:is_valid() then
    State.score = State.score + self.score
    State.kills = State.kills + 1
    for _ = 1, 5 do
      aether.spawn("scripts/puff.lua", { x = self.x, y = self.y, color = self.base_color })
    end
    self:destroy()
  end
end

return Enemy
