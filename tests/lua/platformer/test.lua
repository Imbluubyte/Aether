local test = aether.test
test.run(function()
  test.wait(0.6)
  local player
  for _, n in ipairs(aether.node.root():children()) do if n.layer == "player" then player = n end end
  test.check(player ~= nil, "player exists")
  test.check(player.on_floor, "player lands on the floor")
  local x0, y0 = player.x, player.y
  aether.test.inject("right", 1)
  test.wait(0.5)
  aether.test.inject("right", 0)
  test.check(player.x > x0 + 80, "runs right")
  aether.test.inject("jump", 1)
  test.wait_frames(3)
  aether.test.inject("jump", 0)
  test.wait_frames(10)
  test.check(player.y < y0 - 20 and not player.on_floor, "jumps (y=" .. player.y .. ")")
  test.wait(2)
  test.check(player.on_floor, "lands again")
  test.check(aether.test.error_count() == 0, "no script errors")
end)
