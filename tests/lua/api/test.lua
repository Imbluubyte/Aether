local test = aether.test
local check = test.check

test.run(function()
  -- ---- nodes and properties
  local s = aether.node.create("sprite", { name = "s", x = 5, y = 6, w = 20, h = 10, color = "red", z = 3 })
  check(s.x == 5 and s.y == 6 and s.w == 20, "props applied")
  check(s.type == "sprite", "type")
  s.rotation = 90
  check(math.abs(s.rotation - 90) < 1e-3, "rotation in degrees")
  s.color = "#00ff00"
  check(s.color.g == 1 and s.color.r == 0, "hex colour")
  s.opacity = 0.5
  check(s.color.a == 0.5, "opacity")
  check(not pcall(function() s.x = "oops" end), "type error on bad property value")
  check(not pcall(function() s.alive = false end), "read-only property rejected")
  s.my_field = 42
  check(s.my_field == 42, "custom instance field")

  -- ---- hierarchy / world transform
  local parent = aether.node.create("node", { x = 100, y = 100 })
  local child = aether.node.create("sprite", { x = 10, y = 0, w = 4, h = 4 })
  parent:add_child(child)
  test.wait_frames(2)
  check(child.parent == parent, "parent link")
  check(math.abs(child.world_x - 110) < 1e-3 and math.abs(child.world_y - 100) < 1e-3, "world transform")
  check(not pcall(function() child:add_child(parent) end), "cycle rejected")
  check(parent:find("nothing") == nil, "find miss")

  -- ---- groups, destroy, stale handles
  child:add_to_group("g")
  check(aether.group_count("g") == 1 and #aether.group("g") == 1, "group count")
  child:destroy()
  check(not child:is_valid(), "destroyed node invalid")
  check(aether.group_count("g") == 0, "group pruned")
  check(not pcall(function() return child.x end), "stale handle access errors, not crashes")
  check(child.alive == false, "alive is readable on destroyed nodes")
  test.wait_frames(2)
  check(tostring(child):find("destroyed"), "tostring of destroyed node")

  -- ---- spawn/class scripts
  local Thing = aether.load_script("scripts/thing.lua")
  local a = aether.spawn("scripts/thing.lua", { x = 0, y = 0, hp = 10 })
  local b = aether.spawn("scripts/thing.lua", { x = 100, y = 0 })
  check(a.hp == 10 and b.hp == 3, "per-instance props over class defaults")
  check(a:hit(4) == 6 and b.hp == 3, "methods operate on the instance")
  check(Thing.created == 2, "ready called once per spawn")
  check(aether.group_count("things") == 2, "class props groups applied")
  a:destroy()
  check(Thing.destroyed_count == 1, "destroyed callback")
  b:destroy()

  -- ---- declarative tree
  local tree = aether.node.build({
    type = "node", name = "root_t", x = 10,
    children = { { type = "sprite", name = "c1", w = 2, h = 2 }, { script = "scripts/thing.lua", name = "c2" } },
  })
  check(tree:get_child("c1") ~= nil and tree:get_child("c2") ~= nil, "build children")
  tree:destroy()
  test.wait_frames(2)

  -- ---- timers
  local fired, every = 0, 0
  aether.timer.after(0.2, function() fired = fired + 1 end)
  local id = aether.timer.every(0.1, function() every = every + 1 end)
  test.wait(0.55)
  check(fired == 1, "after fires once")
  check(every >= 4 and every <= 6, "every repeats (" .. every .. ")")
  aether.timer.cancel(id)
  local n = every
  test.wait(0.3)
  check(every == n, "cancelled timer stops")
  local owner = aether.node.create("node")
  local owned = 0
  owner:every(0.05, function() owned = owned + 1 end)
  test.wait(0.2)
  owner:destroy()
  local o = owned
  test.wait(0.3)
  check(owned == o and o > 0, "node timers die with their node")

  -- ---- tween
  local tw = aether.node.create("sprite", { x = 0, w = 1, h = 1 })
  local done = false
  aether.tween(tw, { x = 100 }, 0.3, { on_done = function() done = true end })
  test.wait(0.5)
  check(done and math.abs(tw.x - 100) < 1e-3, "tween finished at target")
  tw:destroy()

  -- ---- save / load
  local ok = aether.save("slot", { score = 12, name = "x", nested = { 1, 2, 3 }, flag = true })
  check(ok, "save ok")
  local back = aether.load("slot")
  check(back.score == 12 and back.name == "x" and back.nested[3] == 3 and back.flag == true, "save round trip")
  check(aether.load("missing") == nil, "missing save is nil")
  check(select(1, aether.save("bad", { f = print })) == false, "functions are not saveable")
  aether.storage.write("evil.sav", "os.exit()")
  check(aether.load("evil") == nil, "malicious save data is not executed")
  aether.delete_save("slot")
  check(not aether.has_save("slot"), "delete save")

  -- ---- input
  aether.input.bind("act", "key:space", "pad:south")
  check(not pcall(aether.input.bind, "act", "key:nonsense"), "bad binding raises")
  check(not aether.input.is_down("act"), "idle")
  aether.test.inject("act", 1)
  test.wait_frames(2)
  check(aether.input.is_down("act") and aether.input.strength("act") == 1, "injected action")
  aether.test.inject("act", 0)
  test.wait_frames(2)
  check(not aether.input.is_down("act"), "released")

  -- ---- physics: layers, collision events, queries, raycast, gravity + solid ground
  aether.physics.set_layers({ "hero", "foe", "ground" })
  local before = Thing.enters
  local hero = aether.node.create("body", { x = 500, y = 500, w = 20, h = 20, layer = "hero", mask = "foe", body_type = "kinematic" })
  local foe = aether.spawn("scripts/thing.lua", { x = 505, y = 500, layer = "foe", mask = "hero" })
  test.wait_frames(3)
  check(Thing.enters > before, "collision enter dispatched to script")
  check(#aether.physics.query_circle(500, 500, 30, "foe") == 1, "query_circle respects mask")
  check(#aether.physics.query_circle(500, 500, 30, "ground") == 0, "query_circle layer filter")
  check(aether.physics.nearest(500, 500, 100, "foe") == foe, "nearest")
  hero:destroy(); foe:destroy()
  local floor = aether.node.create("body", { x = 0, y = 1000, w = 400, h = 20, layer = "ground", body_type = "static" })
  local ray = aether.physics.raycast(0, 900, 0, 1100, "ground")
  check(ray and math.abs(ray.y - 990) < 0.01 and ray.ny == -1, "raycast hit point and normal")
  aether.physics.set_gravity(0, 800)
  local box = aether.node.create("body", { x = 0, y = 900, w = 20, h = 20, layer = "hero", mask = "ground", body_type = "dynamic" })
  test.wait(1.0)
  check(box.on_floor and math.abs(box.y - 980) < 1, "dynamic body rests on static ground (y=" .. box.y .. ")")
  aether.physics.set_gravity(0, 0)
  box:destroy(); floor:destroy()

  -- ---- coroutines inside scripts (bindings must work off the main thread)
  local co = coroutine.wrap(function()
    local n1 = aether.node.create("sprite", { name = "co_node", w = 1, h = 1 })
    coroutine.yield()
    n1:destroy()
    return aether.spawn("scripts/thing.lua", {})
  end)
  co()
  check(aether.find("co_node") ~= nil, "create from coroutine")
  local sp = co()
  check(sp:is_valid(), "spawn from coroutine")
  sp:destroy()

  -- ---- scene change
  aether.scene.change("scenes/other.lua")
  test.wait_frames(3)
  check(aether.scene.current() == "scenes/other.lua", "scene switched")
  check(aether.find("from_other") ~= nil, "new scene ready ran")
  check(aether.find("co_node") == nil, "old scene nodes removed")

  -- ---- error resilience: a failing update must not stop the engine
  local errs = aether.test.error_count()
  local bad = aether.node.create("node")
  bad:set_script({ update = function() error("boom") end })
  test.wait_frames(3)
  check(aether.test.error_count() > errs, "script error counted")
  bad:destroy()
  aether.test.clear_errors()
  check(aether.frame() > 10, "engine kept running")
end)
