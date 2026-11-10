-- Projectile: flies straight, damages the first enemy it touches.
local Bullet = {
  type = "body",
  damage = 1,
  props = { w = 12, h = 6, color = "yellow", body_type = "kinematic", layer = "bullet", mask = "enemy", z = 5 },
}

function Bullet:ready()
  self:after(1.5, function() self:destroy() end)
end

function Bullet:on_collision_enter(other)
  if other:in_group("enemies") then
    other:take_damage(self.damage)
    self:destroy()
  end
end

return Bullet
