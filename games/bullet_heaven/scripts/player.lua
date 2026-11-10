-- Player: moves with the "move" action, auto-fires at the nearest enemy.
local State = require("scripts.state")

local Player = {
  type = "body",
  speed = 260,
  fire_interval = 0.35,
  bullet_speed = 520,
  inv = 0,
  props = {
    texture = "assets/hero.png", frame_w = 16, frame_h = 16, w = 36, h = 36,
    body_type = "kinematic", layer = "player", mask = "none", groups = { "player" }, z = 10,
    animations = {
      idle = { frames = { 1, 2 }, fps = 3 },
      walk = { frames = { 3, 4 }, fps = 10 },
    },
  },
}

function Player:ready()
  self:play("idle")
  self:every(self.fire_interval, function() self:shoot() end)
end

function Player:shoot()
  if State.over then return end
  local target = aether.nearest_in_group("enemies", self.x, self.y, 520)
  if not target then return end
  local dx, dy = self:direction_to(target)
  aether.spawn("scripts/bullet.lua", {
    x = self.x, y = self.y, vx = dx * self.bullet_speed, vy = dy * self.bullet_speed, rotation = math.deg(math.atan(dy, dx)),
  })
end

function Player:update(dt)
  if State.over then
    self.vx, self.vy = 0, 0
    return
  end
  local mx, my = aether.input.vector("move")
  self.vx, self.vy = mx * self.speed, my * self.speed
  if mx ~= 0 or my ~= 0 then self:play("walk") else self:play("idle") end
  if mx < 0 then self.flip_x = true elseif mx > 0 then self.flip_x = false end

  -- Contact damage with invulnerability frames.
  self.inv = math.max(0, self.inv - dt)
  if self.inv <= 0 then
    local touching = aether.physics.query_circle(self.x, self.y, 14, "enemy")
    if #touching > 0 then
      State.hp = State.hp - 1
      self.inv = 0.8
      aether.camera.shake(10, 0.25)
    end
  end
  self.opacity = (self.inv > 0 and math.floor(self.inv * 20) % 2 == 0) and 0.35 or 1
end

return Player
