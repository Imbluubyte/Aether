// Lua-side part of the Aether API, compiled into the engine. Keep in sync with docs/LUA_GUIDE.md.
#include "lua_vm.h"

namespace aether {

const char* kPreludeSource = R"LUA(
local aether = aether

-- ---------------------------------------------------------------- math helpers
local math_ = {}
aether.math = math_
function math_.clamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end
function math_.lerp(a, b, t) return a + (b - a) * t end
function math_.sign(v) if v > 0 then return 1 elseif v < 0 then return -1 end return 0 end
function math_.length(x, y) return math.sqrt(x * x + y * y) end
function math_.distance(x1, y1, x2, y2) local dx, dy = x2 - x1, y2 - y1 return math.sqrt(dx * dx + dy * dy) end
function math_.normalize(x, y)
  local l = math.sqrt(x * x + y * y)
  if l < 1e-9 then return 0, 0 end
  return x / l, y / l
end
function math_.move_toward(v, target, delta)
  if math.abs(target - v) <= delta then return target end
  return v + math_.sign(target - v) * delta
end
-- Direction vector for an angle in degrees (0 = right, 90 = down because y points down).
function math_.dir(deg) local r = math.rad(deg) return math.cos(r), math.sin(r) end
function math_.angle(x, y) return math.deg(math.atan(y, x)) end
function math_.random(a, b)
  if a == nil then return math.random() end
  if b == nil then return math.random(a) end
  return math.random(a, b)
end
function math_.random_range(a, b) return a + math.random() * (b - a) end
function math_.choice(list) return list[math.random(#list)] end
function math_.seed(n) math.randomseed(n) end
-- Random point on a ring around (cx, cy).
function math_.random_on_ring(cx, cy, r)
  local a = math.random() * math.pi * 2
  return cx + math.cos(a) * r, cy + math.sin(a) * r
end

-- ---------------------------------------------------------------- tables
function aether.shuffle(list)
  for i = #list, 2, -1 do local j = math.random(i) list[i], list[j] = list[j], list[i] end
  return list
end
function aether.copy(t, deep)
  if type(t) ~= "table" then return t end
  local r = {}
  for k, v in pairs(t) do r[k] = deep and aether.copy(v, true) or v end
  return r
end

-- ---------------------------------------------------------------- serialisation (saves)
local function ser(v, out, depth)
  local t = type(v)
  if t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then error("cannot save NaN/infinite numbers") end
    if math.type(v) == "integer" then out[#out + 1] = tostring(v) else out[#out + 1] = string.format("%.17g", v) end
  elseif t == "boolean" then out[#out + 1] = v and "true" or "false"
  elseif t == "string" then out[#out + 1] = string.format("%q", v)
  elseif t == "table" then
    if depth > 16 then error("save data nested too deeply (or contains a cycle)") end
    out[#out + 1] = "{"
    for k, val in pairs(v) do
      local kt = type(k)
      if kt == "string" or kt == "number" or kt == "boolean" then
        out[#out + 1] = "["
        ser(k, out, depth + 1)
        out[#out + 1] = "]="
        ser(val, out, depth + 1)
        out[#out + 1] = ","
      end
    end
    out[#out + 1] = "}"
  elseif v == nil then out[#out + 1] = "nil"
  else error("cannot save a value of type " .. t .. " (only numbers, strings, booleans and tables)") end
end

function aether.serialize(value)
  local out = {}
  ser(value, out, 0)
  return table.concat(out)
end

function aether.deserialize(text)
  -- Empty environment: save data can never call functions or touch globals.
  local fn, err = load("return " .. text, "=save", "t", {})
  if not fn then return nil, err end
  local ok, res = pcall(fn)
  if not ok then return nil, res end
  return res
end

-- aether.save("slot1", {score = 10}) -> true | false, err
function aether.save(slot, data)
  local ok, text = pcall(aether.serialize, data)
  if not ok then return false, text end
  if not aether.storage.write(slot .. ".sav", text) then return false, "could not write save file" end
  return true
end

-- aether.load("slot1") -> table | nil
function aether.load(slot)
  local text = aether.storage.read(slot .. ".sav")
  if not text then return nil end
  local v = aether.deserialize(text)
  return v
end
function aether.has_save(slot) return aether.storage.exists(slot .. ".sav") end
function aether.delete_save(slot) return aether.storage.remove(slot .. ".sav") end

-- ---------------------------------------------------------------- easing / tween
local ease = {}
aether.ease = ease
function ease.linear(t) return t end
function ease.in_quad(t) return t * t end
function ease.out_quad(t) return 1 - (1 - t) * (1 - t) end
function ease.in_out_quad(t) if t < 0.5 then return 2 * t * t end return 1 - ((-2 * t + 2) ^ 2) / 2 end
function ease.out_back(t) local c1, c3 = 1.70158, 2.70158 return 1 + c3 * ((t - 1) ^ 3) + c1 * ((t - 1) ^ 2) end

local tweens = {}
-- aether.tween(node, {x = 100, y = 50}, 0.5, {ease = aether.ease.out_quad, on_done = fn}) -> handle
function aether.tween(node, goals, duration, opts)
  opts = opts or {}
  local tw = { node = node, from = {}, to = goals, t = 0, d = math.max(duration, 1e-4), ease = opts.ease or ease.linear, on_done = opts.on_done }
  for k, v in pairs(goals) do tw.from[k] = node[k] end
  tweens[#tweens + 1] = tw
  return tw
end
function aether.cancel_tween(tw) tw.cancelled = true end
function aether._update_tweens(dt)
  local i = 1
  while i <= #tweens do
    local tw = tweens[i]
    if tw.cancelled or not tw.node:is_valid() then
      table.remove(tweens, i)
    else
      tw.t = tw.t + dt
      local k = math.min(tw.t / tw.d, 1)
      local e = tw.ease(k)
      for key, to in pairs(tw.to) do tw.node[key] = tw.from[key] + (to - tw.from[key]) * e end
      if k >= 1 then
        table.remove(tweens, i)
        if tw.on_done then tw.on_done(tw.node) end
      else
        i = i + 1
      end
    end
  end
end
function aether._reset_tweens() tweens = {} end

-- ---------------------------------------------------------------- test harness
-- Used by the headless runner and `aether.test.run`. Coroutine-based: wait(seconds) / wait_frames(n).
local test = aether.test
local runner
local waitLeft = 0
local failures = 0
function test.wait(sec) waitLeft = sec; coroutine.yield() end
function test.wait_frames(n) waitLeft = -n; coroutine.yield() end
function test.check(cond, message)
  if not cond then failures = failures + 1; aether.error("TEST FAILED: " .. (message or "check failed")) end
  return cond
end
function test.failures() return failures end
function test.run(fn)
  runner = coroutine.create(function()
    local ok, err = xpcall(fn, aether._traceback)
    if not ok then failures = failures + 1; aether.error("TEST ERROR: " .. tostring(err)) end
  end)
end
function aether._test_tick(dt)
  if not runner then return end
  if waitLeft > 0 then waitLeft = waitLeft - dt; if waitLeft > 0 then return end
  elseif waitLeft < 0 then waitLeft = waitLeft + 1; if waitLeft < 0 then return end end
  waitLeft = 0
  if coroutine.status(runner) == "dead" then return end
  local ok, err = coroutine.resume(runner)
  if not ok then failures = failures + 1; aether.error("TEST ERROR: " .. tostring(err)) end
  if coroutine.status(runner) == "dead" then
    runner = nil
    aether.test.finish(failures == 0 and 0 or 1)
  end
end
)LUA";

}  // namespace aether
