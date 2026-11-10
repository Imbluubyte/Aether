-- Headless smoke test: the game must run for a while, spawn enemies, and not raise script errors.
local State = require("scripts.state")
local test = aether.test

test.run(function()
  test.wait(1.0)
  test.check(aether.group_count("player") == 1, "player exists")
  test.check(aether.group_count("enemies") > 0, "enemies spawned")

  -- Walk right for a bit; the player node must move.
  local p = aether.group("player")[1]
  local x0 = p.x
  aether.test.inject("move_right", 1)
  test.wait(1.0)
  aether.test.inject("move_right", 0)
  test.check(p.x > x0 + 100, "player moved right (x=" .. tostring(p.x) .. ")")

  -- Let the auto-fire and spawner run; something should die.
  test.wait(15)
  test.check(State.kills > 0 or State.hp < State.max_hp, "combat happened")
  test.check(aether.group_count("enemies") <= 250, "enemy cap respected")
  test.check(aether.test.error_count() == 0, "no script errors")
  aether.test.screenshot("/tmp/aether_bullet_heaven.ppm")
end)
