-- Bullet Heaven demo: the scene script. Attached to the scene root; see docs/LUA_GUIDE.md.
local State = require("scripts.state")

local Game = {}

local MAX_ENEMIES = 250

function Game:ready()
  State.reset()
  local saved = aether.load("profile")
  if saved then State.best = saved.best or 0 end

  aether.physics.set_layers({ "player", "enemy", "bullet" })

  aether.input.bind("move_left", "key:a", "key:left", "axis:leftx-", "pad:dpad_left")
  aether.input.bind("move_right", "key:d", "key:right", "axis:leftx+", "pad:dpad_right")
  aether.input.bind("move_up", "key:w", "key:up", "axis:lefty-", "pad:dpad_up")
  aether.input.bind("move_down", "key:s", "key:down", "axis:lefty+", "pad:dpad_down")
  aether.input.bind("restart", "key:r", "pad:start")
  aether.input.virtual_stick("move", { anchor = "bottom_left", x = 170, y = -170, radius = 110 })

  self.player = aether.spawn("scripts/player.lua", { x = 0, y = 0 })
  aether.camera.follow(self.player, 8)

  -- A new wave every 0.8 seconds; the timer is owned by this node.
  self:every(0.8, function() self:spawn_wave() end)
end

function Game:spawn_enemy(kind, x, y)
  if aether.group_count("enemies") >= MAX_ENEMIES then return end
  local t = State.time
  if kind == "fast" then
    aether.spawn("scripts/enemy.lua", { x = x, y = y, hp = 1, speed = 150, color = "yellow", w = 18, h = 18, score = 15 })
  elseif kind == "tank" then
    aether.spawn("scripts/enemy.lua", { x = x, y = y, hp = 8 + math.floor(t / 20), speed = 50, color = "purple", w = 38, h = 38, score = 40 })
  else
    aether.spawn("scripts/enemy.lua", { x = x, y = y, hp = 2 + math.floor(t / 30), speed = 85 + math.min(t, 60), color = "red", score = 10 })
  end
end

function Game:spawn_wave()
  if State.over or not self.player:is_valid() then return end
  local sw, sh = aether.screen.size()
  local ring = math.sqrt(sw * sw + sh * sh) / 2 + 60
  local count = 3 + math.floor(State.time / 8)
  for _ = 1, count do
    local x, y = aether.math.random_on_ring(self.player.x, self.player.y, ring)
    local roll = math.random()
    local kind = "basic"
    if State.time > 15 and roll < 0.2 then kind = "fast" end
    if State.time > 30 and roll > 0.9 then kind = "tank" end
    self:spawn_enemy(kind, x, y)
  end
end

function Game:update(dt)
  if State.over then
    if aether.input.pressed("restart") or aether.input.pointer_pressed() then aether.scene.reload() end
    return
  end
  State.time = State.time + dt
  if State.hp <= 0 then
    State.over = true
    if State.score > State.best then
      State.best = State.score
      aether.save("profile", { best = State.best })
    end
  end
end

-- World-space drawing: a scrolling grid so movement is visible.
function Game:draw()
  local cx, cy = aether.camera.position()
  local sw, sh = aether.screen.size()
  local step = 128
  local x0 = math.floor((cx - sw / 2) / step) * step
  local y0 = math.floor((cy - sh / 2) / step) * step
  local c = { 1, 1, 1, 0.06 }
  for x = x0, cx + sw / 2 + step, step do aether.draw.line(x, cy - sh / 2 - step, x, cy + sh / 2 + step, c, 1) end
  for y = y0, cy + sh / 2 + step, step do aether.draw.line(cx - sw / 2 - step, y, cx + sw / 2 + step, y, c, 1) end
end

-- Screen-space HUD.
function Game:draw_ui()
  local sw, sh = aether.screen.size()
  aether.draw.text("SCORE " .. State.score, 20, 16, { size = 3, color = "white" })
  aether.draw.text(string.format("TIME %d", math.floor(State.time)), sw - 20, 16, { size = 3, color = "white", align = "right" })
  -- health pips
  for i = 1, State.max_hp do
    local col = i <= State.hp and "red" or { 1, 1, 1, 0.15 }
    aether.draw.rect(20 + (i - 1) * 30, 56, 22, 22, col)
  end
  aether.draw.text("BEST " .. State.best, 20, 88, { size = 2, color = "gray" })
  if State.over then
    aether.draw.rect(0, sh / 2 - 90, sw, 180, { 0, 0, 0, 0.6 })
    aether.draw.text("GAME OVER", sw / 2, sh / 2 - 60, { size = 8, color = "red", align = "center" })
    aether.draw.text("SCORE " .. State.score .. "   KILLS " .. State.kills, sw / 2, sh / 2 + 10, { size = 3, color = "white", align = "center" })
    aether.draw.text("PRESS R OR TAP TO RESTART", sw / 2, sh / 2 + 50, { size = 2, color = "gray", align = "center" })
  end
end

return Game
