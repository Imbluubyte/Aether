local Player = {
  type = "body", speed = 240, jump_speed = 560, coyote = 0, 
  props = { w = 28, h = 40, color = "cyan", body_type = "dynamic", layer = "player", mask = "ground" },
}

function Player:update(dt)
  local dir = aether.input.strength("right") - aether.input.strength("left")
  self.vx = dir * self.speed
  -- "coyote time": allow a jump for a short moment after leaving a ledge
  if self.on_floor then self.coyote = 0.1 else self.coyote = self.coyote - dt end
  if aether.input.pressed("jump") and self.coyote > 0 then
    self.vy = -self.jump_speed
    self.coyote = 0
  end
end

return Player
