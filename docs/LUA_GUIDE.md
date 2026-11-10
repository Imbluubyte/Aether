# Aether Lua Scripting Guide

Aether games are written in **Lua 5.4**. The C++ engine runs the loop, rendering, physics, audio and input; your scripts decide what happens.
Everything lives in one global table, `aether`. Exported games need no Kotlin or Java: engine + Lua + your assets are enough.

Contents: [1 Lua basics](#1-lua-basics) · [2 Project layout](#2-project-layout) · [3 Scripts & callbacks](#3-scripts-and-callbacks) · [4 Nodes](#4-nodes) ·
[5 Scenes](#5-scenes) · [6 Input](#6-input) · [7 Physics](#7-physics-and-collisions) · [8 Animation & rendering](#8-animation-and-rendering) · [9 Camera](#9-camera) ·
[10 Audio](#10-audio) · [11 Timers & tweens](#11-timers-and-tweens) · [12 Saves](#12-saving-and-loading) · [13 Utilities](#13-utilities) · [14 Testing](#14-testing-your-game) ·
[15 Recipes](#15-recipes) · [16 Pitfalls](#16-pitfalls) · [17 Reference](#17-quick-reference)

Conventions: **y points down**, angles are in **degrees**, time is in **seconds**, positions are in virtual pixels (the game's design resolution).

---

## 1. Lua basics

```lua
-- comment
local score = 0                 -- local variable (always prefer local)
local name, alive = "orc", true -- strings, booleans; nil means "nothing"
local speed = 3.5               -- numbers (integers and floats)
print("hello", score)           -- print goes to the console / logcat

-- if / elseif / else
if score > 10 then print("high") elseif score == 0 then print("zero") else print("low") end

-- loops
for i = 1, 5 do print(i) end                  -- 1..5
for i = 10, 1, -2 do print(i) end             -- 10,8,6,4,2
local list = { "a", "b", "c" }                -- tables are Lua's only container; arrays start at 1
for i, v in ipairs(list) do print(i, v) end   -- ordered (arrays)
local stats = { hp = 3, speed = 80 }          -- table used as a record / dictionary
for k, v in pairs(stats) do print(k, v) end   -- unordered
while score < 3 do score = score + 1 end
table.insert(list, "d"); table.remove(list, 1); print(#list)   -- append, remove first, length

-- functions are values; they can be stored, passed and returned
local function add(a, b) return a + b end
local function pair() return 1, 2 end            -- multiple return values
local x, y = pair()

-- "classes": a table of functions; methods use ':' so `self` is passed automatically
local Counter = {}
function Counter:inc() self.n = (self.n or 0) + 1 end
```

Operators: `+ - * / // % ^` (`//` integer division), comparison `== ~= < <= > >=`, logic `and or not`, concatenation `..`, length `#`.
Only `nil` and `false` are false (`0` and `""` are true). Useful libraries: `math` (`floor`, `sqrt`, `sin`, `random`...), `string` (`format`, `sub`, `find`, `gsub`), `table` (`insert`, `remove`, `sort`, `concat`), `utf8`, `os.time/os.clock/os.date`.
`require("scripts.util")` loads `scripts/util.lua` from your game (cached). File/OS access (`io`, `os.execute`, `dofile`...) is **not** available.

---

## 2. Project layout

```
my_game/
  game.lua          -- optional settings (below)
  main.lua          -- the main scene script (required; see "main" below)
  scripts/          -- enemy.lua, player.lua ...
  scenes/           -- more scene scripts
  assets/           -- png/jpg/bmp/tga images, wav/mp3/flac audio
  tests/smoke.lua   -- automated tests (not shipped in exports)
```

`game.lua` returns a settings table (all keys optional):

| key | default | meaning |
|---|---|---|
| `title` | `"Aether Game"` | window / app title |
| `width`, `height` | `1280`, `720` | design resolution in virtual pixels |
| `stretch` | `"expand"` | `"expand"`: keep `height`, widen to the screen's aspect (`aether.screen.width()` varies). `"fit"`: fixed size with letterbox bars |
| `main` | `"main.lua"` | script run first |
| `background` | `{0.07,0.08,0.11}` | clear colour `{r,g,b}` 0..1 |
| `smooth` | `false` | `true` = linear texture filtering; `false` = crisp pixels |
| `fixed_hz` | `60` | physics / `fixed_update` rate (10..240) |

---

## 3. Scripts and callbacks

A script is a file that **returns a table** (the *class*). Attach it to a node and its functions become that node's behaviour.
The `main` script is attached to the scene root. Other scripts are attached with `aether.spawn`, `node:set_script` or `script =` in `aether.node.build`.

```lua
local Enemy = {
  type = "body",                 -- node type created by aether.spawn (default "node")
  hp = 3, speed = 80,            -- defaults; read as self.hp. Instances can override them
  props = { w = 24, h = 24 },   -- native properties applied to every new instance
}
function Enemy:ready() end
function Enemy:update(dt) self.x = self.x + self.speed * dt end
return Enemy
```

Callbacks (all optional, `self` is the node):

| callback | when |
|---|---|
| `ready()` | once, after the node is created, props applied and the script attached |
| `update(dt)` | every frame (`dt` = seconds, scaled by `aether.time_scale`) |
| `fixed_update(dt)` | every physics step (fixed `dt`, default 1/60) |
| `paused_update(dt)` | every frame *while* `aether.pause(true)` is active (build pause menus here); `update` is skipped |
| `draw()` | after nodes, in **world** coordinates: immediate drawing (`aether.draw.*`) |
| `draw_ui()` | last, in **screen** coordinates (HUD) |
| `on_collision_enter(other)` / `on_collision_exit(other)` | a body overlap starts / ends (see §7) |
| `on_animation_finished(name)` | a non-looping animation reached its last frame |
| `destroyed()` | just before the node is destroyed (clean up; the node is still readable here) |

Errors inside callbacks are caught: the message and a traceback go to the console, the game keeps running, and the node's other callbacks still work.

**Instance data**: assigning to a name that isn't a native property stores it on the instance: `self.cooldown = 0.5`. Passing unknown keys in a props table does the same: `aether.spawn("scripts/enemy.lua", { hp = 5 })` sets `self.hp = 5` *before* `ready()` runs.
Lookup order for `self.name`: native property → native method → instance field → class field.
Do **not** use native property names (`x`, `y`, `type`, `name`, `color`, `scale`, `layer`, `mask`, `text`, ...) for your own fields.

---

## 4. Nodes

A **node** is an object in the scene tree. Types: `"node"` (empty/transform only), `"sprite"`, `"anim"` (alias `"animated_sprite"`, a sprite meant for animation), `"body"` (collider; may also draw), `"label"` (text).
Children inherit position, rotation and scale from their parent. Draw order: by `z` (higher on top), then creation order.

### Creating

```lua
local s = aether.node.create("sprite", { x = 100, y = 50, w = 32, h = 32, color = "red" })
local hero = aether.spawn("scripts/player.lua", { x = 0, y = 0 })       -- node + script
local ui = aether.node.build({                                          -- declarative tree
  type = "node", name = "hud",
  children = {
    { type = "label", name = "score", text = "0", x = 20, y = 20, screen_space = true },
    { script = "scripts/minimap.lua", name = "map" },
  },
})
```

| function | returns | notes |
|---|---|---|
| `aether.node.create(type [, props])` | node | child of the scene root |
| `aether.spawn(script [, props])` | node | `script` = path string or a class table. Creates the node type from `script.type`, applies `script.props`, then `props`, then calls `ready()` |
| `aether.node.build(spec)` | node | spec = `{ type=, script=, name=, <props>, children={...} }` |
| `aether.node.root()` | node | scene root |
| `aether.find(name)` | node or nil | first node with that name (depth-first search) |
| `aether.load_script(path)` | class table | load/cached class without creating a node |

### Properties (read/write unless noted)

| group | properties |
|---|---|
| identity | `name`, `type` (r/o), `alive` (r/o), `script` (r/o path), `parent` (node or `nil` = root) |
| transform | `x`, `y` (local), `world_x`, `world_y` (r/o), `rotation` (degrees), `scale`, `scale_x`, `scale_y` (negative mirrors) |
| drawing | `visible`, `z`, `w`, `h`, `color`, `opacity`, `texture` (path or nil), `region` (`{x,y,w,h}` pixels in the texture), `flip_x`, `flip_y`, `origin_x`, `origin_y` (0..1, default 0.5 = centre), `screen_space` |
| animation | `frame_w`, `frame_h`, `frame` (1-based cell), `anim`, `animations`, `anim_speed`, `playing` (r/o) |
| label | `text`, `text_size` (pixel multiplier of the 5x7 font), `align` (`"left"`,`"center"`,`"right"`) |
| physics | `body_type`, `shape`, `radius`, `layer`, `mask`, `vx`, `vy`, `gravity_scale`, `active`, `on_floor`/`on_ceiling`/`on_wall` (r/o) |

`w`/`h` = display size in world units. If `0`, the size comes from `frame_w/h`, then `region`, then the texture size. Bodies use the same size for their box collider.
Colours: `{r,g,b,a}` (0..1, array or named), `0xRRGGBB`, `"#rrggbb"` / `"#rrggbbaa"`, or a name: `white black red green blue yellow orange purple cyan magenta gray pink brown transparent`.
Setting a wrong type (`node.x = "a"`) or a read-only property raises a Lua error naming the property.

### Methods (call with `:`)

| method | description |
|---|---|
| `node:destroy()` | remove the node and its children (runs `destroyed()` callbacks, cancels its timers) |
| `node:is_valid()` | `true` while the node exists. Always check stored references that might have died |
| `node:add_child(child)` / set `child.parent = node` | reparent (cycles are rejected) |
| `node:get_child(name)` / `node:find(name)` | direct child / any descendant by name (or nil) |
| `node:children()` | array of children |
| `node:add_to_group(g)` / `remove_from_group(g)` / `in_group(g)` / `groups()` | group membership |
| `node:set(props)` | apply a props table; returns the node |
| `node:set_script(class_or_path [, props])` | attach/replace the script and call `ready()` |
| `node:after(seconds, fn)` | one-shot timer owned by the node (returns timer id) |
| `node:every(seconds, fn [, count])` | repeating timer owned by the node (stops when it is destroyed) |
| `node:play(name [, restart])` / `node:stop_anim()` | animations (§8) |
| `node:move(dx, dy)` → `hit_x, hit_y` | move now, stopped by solid static bodies; returns whether each axis was blocked |
| `node:distance_to(node)` or `(x, y)` | distance between world positions |
| `node:angle_to(node or x,y)` | angle in degrees to the target |
| `node:direction_to(node or x,y)` → `dx, dy` | unit vector towards the target (`0,0` if on top of it) |
| `node:look_at(node or x,y)` | rotate so +x faces the target |

### Groups

Groups tag nodes ("enemies", "pickups"). Fast to query, automatically pruned when nodes die.

```lua
aether.group("enemies")                 -- array of live nodes
aether.group_count("enemies")           -- number
aether.nearest_in_group("enemies", x, y [, max_distance])   -- node, distance  (or nil)
```
Add via `node:add_to_group("enemies")` or `groups = { "enemies" }` in props.

---

## 5. Scenes

A scene is a script (usually in `scenes/`) attached to the root. Changing scenes destroys all nodes, timers and tweens, clears virtual controls and resets the camera; input bindings, physics layers, gravity, audio volumes and your `require`d modules persist.

```lua
aether.scene.change("scenes/level2.lua")   -- applied at the end of the frame
aether.scene.reload()                      -- restart the current scene
aether.scene.current()                     -- path string
```
Keep data that must survive scene changes in a module (`require("scripts.state")`) or in a save file.

Other game-level functions: `aether.time()` (seconds of game time), `aether.frame()`, `aether.fps()`, `aether.time_scale([s])` (slow-motion: `0.5`; get/set), `aether.pause(bool)`, `aether.is_paused()`, `aether.quit([exit_code])`, `aether.screen.size()` → `w, h`, `aether.screen.width()`, `aether.screen.height()`, `aether.log(...)`, `aether.warn(...)`, `aether.error(...)`.

---

## 6. Input

Games read **actions**, not keys, so the same code works with keyboard, gamepad and touch (and later the Switch).

```lua
aether.input.bind("jump", "key:space", "pad:south")            -- several bindings per action
aether.input.bind("move_left", "key:a", "key:left", "axis:leftx-", "pad:dpad_left")
```

Binding strings:
- `key:<name>` — `a`..`z`, `0`..`9`, `left right up down`, `space enter escape tab backspace shift ctrl alt`, `f1`..`f12`
- `pad:<button>` — `south east west north` (physical positions: Xbox A/B/X/Y = Switch B/A/Y/X), `lb rb back start lstick rstick dpad_up dpad_down dpad_left dpad_right`
- `axis:<axis><+|->` — `leftx lefty rightx righty lt rt`, with `+` or `-` giving that direction's strength (deadzone applied)

| function | description |
|---|---|
| `aether.input.bind(action, binding, ...)` | add bindings (arguments or tables of strings); raises on an unknown binding |
| `aether.input.clear([action])` | remove bindings of one action / all |
| `aether.input.is_down(a)` | held this frame |
| `aether.input.pressed(a)` / `released(a)` | true only on the frame it went down / up |
| `aether.input.strength(a)` | 0..1 (analog) |
| `aether.input.vector(prefix)` → `x, y` | combines `<prefix>_left/_right/_up/_down` into a vector (length ≤ 1) |
| `aether.input.key_pressed(keyname)` | raw key edge (prefer actions) |
| `aether.input.pointer()` → `x, y, down` | first touch / pointer in **virtual** screen coordinates |
| `aether.input.pointer_pressed()` / `pointer_released()` | edge this frame |
| `aether.input.touches()` | array of `{id,x,y}` for all active touches |
| `aether.input.has_touch()` | the device has/uses a touchscreen |
| `aether.input.virtual_stick(prefix [, opts])` | on-screen joystick driving `<prefix>_left/_right/_up/_down`. opts: `anchor` (`bottom_left`, `bottom_right`, `top_left`, `top_right`, `center`), `x`,`y` offset from the anchor, `radius` |
| `aether.input.virtual_button(action [, opts])` | on-screen button. opts: `label`, `anchor`, `x`, `y`, `radius` |
| `aether.input.clear_virtual()` | remove on-screen controls |

Virtual controls are only drawn once a touch has been seen (or on touch devices) and swallow the touches that start on them.

```lua
aether.input.virtual_stick("move", { anchor = "bottom_left", x = 170, y = -170 })
aether.input.virtual_button("jump", { label = "A", anchor = "bottom_right", x = -150, y = -150 })
local mx, my = aether.input.vector("move")     -- works with keys, gamepad and the stick
```

Screen → world for touch aiming: `local wx, wy = aether.camera.screen_to_world(aether.input.pointer())` (see §9).

---

## 7. Physics and collisions

Only `"body"` nodes take part. Each body has a **shape** (box by default, using `w`/`h`; or circle via `radius`), a **body_type**, a **layer** and a **mask**.

| body_type | behaviour |
|---|---|
| `"static"` | never moves by itself; **solid** for dynamic bodies (floors, walls) |
| `"dynamic"` | moved by `vx`/`vy` and gravity; blocked by static bodies whose layer is in its `mask`; sets `on_floor`, `on_ceiling`, `on_wall` |
| `"kinematic"` | moved by `vx`/`vy` only (no gravity, not blocked); ideal for enemies and bullets |
| `"trigger"` | like kinematic; use for zones (overlap events only) |

### Layers and masks

Declare layer names once (up to 32), then refer to them by name:

```lua
aether.physics.set_layers({ "player", "enemy", "bullet", "ground" })
node.layer = "bullet"                  -- the layer this body is on
node.mask  = { "enemy", "ground" }     -- layers this body *reacts to*: "name", {names}, "all", "none" (default "all")
```
A pair overlaps-events fire if **either** body's mask contains the other's layer; each body whose mask matches is told. Dynamic solid collision only considers static bodies in the mover's mask.
Tip: give most bodies `mask = "none"` and let only the "hitter" (bullet, player) listen — far fewer events.

Box colliders are **axis-aligned**: a body's `rotation` rotates its sprite but not its collider (use `radius` for round things). Colliders scale with the node's `scale`.

### Events

```lua
function Bullet:on_collision_enter(other)      -- `other` is the other body's node
  if other:in_group("enemies") then other:take_damage(1); self:destroy() end
end
function Zone:on_collision_exit(other) end
```
Events are delivered after each physics step. Both nodes are guaranteed valid when the callback is called.

### Movement and gravity

```lua
aether.physics.set_gravity(0, 1400)         -- pixels / s², y down (default 0,0)
self.vx, self.vy = 200, 0                    -- velocity in pixels / s
local hit_x, hit_y = self:move(10, 0)        -- immediate solid move
```

### Queries (use the body layers; `mask` optional, same format as above, default all)

| function | returns |
|---|---|
| `aether.physics.query_circle(x, y, radius [, mask])` | array of nodes overlapping the circle |
| `aether.physics.query_rect(x, y, w, h [, mask])` | array of nodes overlapping the rect (x,y = top-left) |
| `aether.physics.nearest(x, y, max_dist [, mask, ignore_node])` | nearest body node or nil |
| `aether.physics.raycast(x1, y1, x2, y2 [, mask])` | `nil` or `{ node=, x=, y=, nx=, ny=, distance= }` (hit point and surface normal) |
| `aether.physics.gravity()` | `gx, gy` |
| `aether.physics.set_cell_size(n)` | broadphase cell size (default 96); set near your typical body size if bodies are very large/small |

Queries see positions as of the last physics step (a freshly created/moved static body is seen immediately).
Debug: `aether.debug.colliders(true)` draws collider outlines (green static, orange movers, yellow triggers).

---

## 8. Animation and rendering

### Textures and spritesheets

```lua
local hero = aether.node.create("anim", {
  texture = "assets/hero.png",         -- png, jpg, bmp, tga (relative to the game folder)
  frame_w = 16, frame_h = 16,          -- the sheet is a grid of equal cells, numbered 1.. left-to-right, top-to-bottom
  w = 48, h = 48,                      -- display size (scales the 16x16 cell)
  animations = {
    idle = { frames = { 1, 2 }, fps = 3 },
    walk = { from = 3, to = 6, fps = 10 },            -- from/to is shorthand for a frame range
    hit  = { frames = { 7, 8, 9 }, fps = 12, loop = false },
  },
})
hero:play("walk")                       -- no-op if already playing; hero:play("walk", true) restarts
hero.anim_speed = 1.5                   -- playback speed multiplier
hero.frame = 4                          -- show a fixed cell (stops the animation)
function Hero:on_animation_finished(name) if name == "hit" then self:play("idle") end end
```
Single images from an atlas: `region = { x = 32, y = 0, w = 16, h = 16 }` (texture pixels). `flip_x = true` mirrors. A texture that fails to load draws a magenta box and logs the error once.
`aether.res.preload({ "assets/a.png", "assets/b.png" })` loads ahead of time (returns how many failed); `aether.res.texture_size(path)` → `w, h` (or nil).

### Immediate drawing (`draw()` = world space, `draw_ui()` = screen space)

| function | notes |
|---|---|
| `aether.draw.rect(x, y, w, h, color)` | filled rectangle (top-left x,y) |
| `aether.draw.rect_outline(x, y, w, h, color [, thickness])` | |
| `aether.draw.line(x1, y1, x2, y2, color [, thickness])` | |
| `aether.draw.circle(cx, cy, r, color [, thickness])` | outline |
| `aether.draw.text(str, x, y, opts)` | opts: `size` (pixel multiplier, default 2), `color`, `align` (`left`/`center`/`right`). Built-in 5x7 font, ASCII, `\n` supported |
| `aether.draw.measure_text(str [, size])` | `width, height` |

Label nodes (`type = "label"`) are the retained version of `draw.text`; add `screen_space = true` for HUD labels that ignore the camera.
Debug: `aether.debug.stats(true)` shows fps / node / body counts; `aether.debug.node_count()`.

---

## 9. Camera

```lua
aether.camera.follow(player, 8)      -- follow a node; smoothing: 0 = snap, larger = snappier ease
aether.camera.unfollow()
aether.camera.set_position(x, y)  /  aether.camera.position() → x, y
aether.camera.set_zoom(2)         /  aether.camera.zoom()
aether.camera.shake(10, 0.3)         -- amplitude (pixels), duration (s)
aether.camera.screen_to_world(sx, sy) → wx, wy
aether.camera.world_to_screen(wx, wy) → sx, sy
```
The camera centres on its position. Nodes with `screen_space = true` and `draw_ui` ignore it.

---

## 10. Audio

Formats: **wav, mp3, flac**. Missing/unsupported files return `nil`/`false` and log a warning (the game continues).

```lua
local h = aether.audio.play("assets/shoot.wav", { volume = 0.8, pitch = 1.2, loop = false })  -- handle or nil
aether.audio.stop(h)
aether.audio.music("assets/theme.mp3", { volume = 0.6, loop = true })   -- one music track at a time; returns bool
aether.audio.stop_music()
aether.audio.volume("master" | "sfx" | "music" [, 0..1])                -- get / set bus volume
```
At most 48 sounds play at once. Audio pauses with `aether.pause(true)` and when the app is backgrounded.

---

## 11. Timers and tweens

```lua
aether.timer.after(2.0, function() print("later") end)              -- global one-shot → id
local id = aether.timer.every(0.5, spawn_wave [, count])            -- repeating; stops after `count` runs if given
aether.timer.cancel(id);  aether.timer.active(id)
self:after(1, fn)   self:every(0.8, fn [, count])                   -- same, but owned by a node (auto-cancelled when it is destroyed)
```
Returning `false` from a repeating callback stops it. Timers use game time (they stop while paused and follow `time_scale`). Prefer node timers inside scripts so they never fire for dead objects.

Tweens animate numeric properties:
```lua
local tw = aether.tween(node, { x = 400, opacity = 0 }, 0.5, { ease = aether.ease.out_quad, on_done = function(n) n:destroy() end })
aether.cancel_tween(tw)
```
Easings: `linear in_quad out_quad in_out_quad out_back` (functions `t → t`, 0..1). A tween stops if its node is destroyed.

---

## 12. Saving and loading

```lua
aether.save("profile", { best = 1200, settings = { music = 0.5 }, unlocked = { "bow", "axe" } })   -- true | false, err
local data = aether.load("profile")        -- table or nil (missing/corrupt)
aether.has_save("profile");  aether.delete_save("profile")
```
Saves hold numbers, strings, booleans and (nested) tables — not functions or nodes. Files are written atomically and live in the game's private storage (loading never executes code).
Low level: `aether.storage.write/read/remove/exists/list` (flat file names), `aether.serialize(v)` / `aether.deserialize(str)`.

Typical pattern: load on start, save when something important changes (game over, settings closed, level complete) — not every frame.

---

## 13. Utilities

`aether.math`: `clamp(v,lo,hi)`, `lerp(a,b,t)`, `sign(v)`, `length(x,y)`, `distance(x1,y1,x2,y2)`, `normalize(x,y)` → `x,y`, `move_toward(v,target,delta)`, `dir(deg)` → `x,y`, `angle(x,y)` → degrees,
`random()` / `random(n)` / `random(a,b)`, `random_range(a,b)` (float), `choice(list)`, `seed(n)`, `random_on_ring(cx,cy,r)` → `x,y`.
`aether.shuffle(list)`, `aether.copy(table [, deep])`.

---

## 14. Testing your game

You can play-test inside the editor (▶ Play) and run **automated tests** the same way, or headlessly on a computer:
```
aether_run my_game --test tests/smoke.lua --frames 3600 [--screenshot out.ppm]
```
In the editor, "Run tests" plays the game with `tests/smoke.lua` and shows failures in the console. A test is a script using a coroutine runner that can wait for game time:

```lua
local test = aether.test
test.run(function()
  test.wait(1.0)                                         -- wait 1 s of game time (test.wait_frames(n) also exists)
  test.check(aether.group_count("enemies") > 0, "enemies spawned")
  aether.test.inject("move_right", 1)                    -- hold an action (inject(action, 0) releases)
  test.wait(0.5)
  aether.test.touch(1, 200, 300, 0)                      -- fake touch: id, x, y, phase 0 down / 1 move / 2 up (physical pixels)
  test.check(aether.test.error_count() == 0, "no script errors")
  aether.test.screenshot("/tmp/shot.ppm")                -- headless runner only
end)                                                     -- finishes with exit code 0 if every check passed, else 1
```
Other helpers: `aether.test.finish([code])`, `aether.test.quads()` (quads drawn last frame), `aether.test.clear_errors()`, `test.failures()`.
Your own modules work in tests through `require`.

---

## 15. Recipes

### 15.1 A reusable enemy

`scripts/enemy.lua` — one script, many different enemies through props:

```lua
local Enemy = {
  type = "body", hp = 3, speed = 85, score = 10, flash = 0,
  props = { w = 26, h = 26, body_type = "kinematic", layer = "enemy", mask = "none", groups = { "enemies" } },
}

function Enemy:ready() self.base_color = self.color end

function Enemy:update(dt)
  local player = aether.nearest_in_group("player", self.x, self.y)
  if player then
    local dx, dy = self:direction_to(player)
    self.vx, self.vy = dx * self.speed, dy * self.speed       -- kinematic bodies move by velocity
  end
  if self.flash > 0 then
    self.flash = self.flash - dt
    if self.flash <= 0 then self.color = self.base_color end
  end
end

function Enemy:take_damage(amount)
  self.hp = self.hp - amount
  self.color = "white"; self.flash = 0.08                      -- hit flash
  if self.hp <= 0 and self:is_valid() then
    aether.spawn("scripts/puff.lua", { x = self.x, y = self.y, color = self.base_color })
    self:destroy()
  end
end

return Enemy
```

### 15.2 Spawning many instances

```lua
-- a specific number, stats overridden per instance
for i = 1, 20 do
  aether.spawn("scripts/enemy.lua", { x = i * 40, y = -200, hp = 2 + i // 5, speed = 60 + i, color = "red" })
end

-- several kinds from one script
local kinds = {
  basic = { hp = 2, speed = 85, color = "red" },
  fast  = { hp = 1, speed = 150, color = "yellow", w = 18, h = 18 },
  tank  = { hp = 8, speed = 50, color = "purple", w = 38, h = 38, score = 40 },
}
local function spawn_kind(kind, x, y)
  local props = aether.copy(kinds[kind]); props.x, props.y = x, y
  return aether.spawn("scripts/enemy.lua", props)
end
```

### 15.3 Bullet-heaven spawning and management

```lua
local MAX_ENEMIES = 250
function Game:ready()
  aether.physics.set_layers({ "player", "enemy", "bullet" })
  self.player = aether.spawn("scripts/player.lua")
  self:every(0.8, function() self:spawn_wave() end)       -- node timer: dies with the scene
end

function Game:spawn_wave()
  if not self.player:is_valid() then return end
  local sw, sh = aether.screen.size()
  local ring = math.sqrt(sw * sw + sh * sh) / 2 + 60          -- just outside the screen
  local count = 3 + math.floor(State.time / 8)                -- difficulty ramps with time
  for _ = 1, count do
    if aether.group_count("enemies") >= MAX_ENEMIES then return end   -- hard cap keeps the frame rate up
    local x, y = aether.math.random_on_ring(self.player.x, self.player.y, ring)
    spawn_kind(math.random() < 0.2 and "fast" or "basic", x, y)
  end
end
```
Management tips: cap the enemy count; give enemies `mask = "none"` and let bullets listen; use `aether.nearest_in_group` instead of scanning lists; destroy bullets with a lifetime timer (`self:after(1.5, function() self:destroy() end)`).
The full runnable game is in `games/bullet_heaven/`.

### 15.4 Player movement and projectiles

```lua
local Player = {
  type = "body", speed = 260, fire_interval = 0.35, bullet_speed = 520,
  props = { w = 36, h = 36, color = "cyan", body_type = "kinematic", layer = "player", mask = "none", groups = { "player" } },
}

function Player:ready()
  self:every(self.fire_interval, function() self:shoot() end)       -- auto-fire
end

function Player:update(dt)
  local mx, my = aether.input.vector("move")                        -- keys, gamepad or on-screen stick
  self.vx, self.vy = mx * self.speed, my * self.speed
end

function Player:shoot()
  local target = aether.nearest_in_group("enemies", self.x, self.y, 520)
  if not target then return end
  local dx, dy = self:direction_to(target)
  aether.spawn("scripts/bullet.lua", { x = self.x, y = self.y, vx = dx * self.bullet_speed, vy = dy * self.bullet_speed,
                                       rotation = math.deg(math.atan(dy, dx)) })
end
return Player
```
```lua
-- scripts/bullet.lua
local Bullet = { type = "body", damage = 1,
  props = { w = 12, h = 6, color = "yellow", body_type = "kinematic", layer = "bullet", mask = "enemy" } }
function Bullet:ready() self:after(1.5, function() self:destroy() end) end
function Bullet:on_collision_enter(other)
  if other:in_group("enemies") then other:take_damage(self.damage); self:destroy() end
end
return Bullet
```
Aim with the pointer instead: `local wx, wy = aether.camera.screen_to_world(aether.input.pointer())`, then `self:direction_to(wx, wy)`.

### 15.5 Collision and health systems

Health lives on the node; damage goes through a method so every source (bullets, spikes, explosions) uses the same rule:

```lua
local Health = {}   -- scripts/health.lua: a helper module (require("scripts.health"))
function Health.attach(node, max)
  node.max_hp, node.hp, node.inv = max, max, 0
end
function Health.damage(node, amount)
  if node.inv > 0 or node.hp <= 0 then return false end
  node.hp = node.hp - amount
  node.inv = node.inv_time or 0.8                       -- invulnerability frames
  if node.on_hurt then node:on_hurt(amount) end
  if node.hp <= 0 and node.on_die then node:on_die() end
  return true
end
function Health.tick(node, dt) node.inv = math.max(0, node.inv - dt) end
return Health
```
```lua
function Player:update(dt)
  Health.tick(self, dt)
  -- contact damage by overlap query (continuous while touching, unlike enter events)
  if #aether.physics.query_circle(self.x, self.y, 14, "enemy") > 0 then Health.damage(self, 1) end
  self.opacity = (self.inv > 0 and math.floor(self.inv * 20) % 2 == 0) and 0.35 or 1     -- blink
end
function Player:on_hurt() aether.camera.shake(10, 0.25) end
function Player:on_die() State.over = true end
```
Area damage: `for _, n in ipairs(aether.physics.query_circle(x, y, 80, "enemy")) do n:take_damage(3) end`.

### 15.6 Timers and game state

```lua
-- scripts/state.lua : a module is a shared, persistent table
local S = {}
function S.reset() S.score, S.time, S.hp, S.over = 0, 0, 5, false end
S.reset()
return S
```
```lua
local State = require("scripts.state")

function Game:update(dt)
  if State.over then
    if aether.input.pressed("restart") then aether.scene.reload() end   -- scene reload; modules persist, so reset them in ready()
    return
  end
  State.time = State.time + dt
end

-- finite states for a boss:
function Boss:set_state(s) self.state = s; self.state_time = 0 end
function Boss:update(dt)
  self.state_time = self.state_time + dt
  if self.state == "chase" and self.state_time > 4 then self:set_state("charge")
  elseif self.state == "charge" and self.state_time > 1 then self:set_state("chase") end
end

-- timers: a power-up that lasts 5 seconds, a pause menu
aether.timer.after(5, function() player.speed = 260 end)
function Menu:paused_update(dt) if aether.input.pressed("pause") then aether.pause(false) end end
```

### 15.7 Platformer

```lua
-- main.lua
function Game:ready()
  aether.physics.set_layers({ "player", "ground" })
  aether.physics.set_gravity(0, 1400)
  aether.input.bind("left", "key:a", "key:left", "axis:leftx-")
  aether.input.bind("right", "key:d", "key:right", "axis:leftx+")
  aether.input.bind("jump", "key:space", "pad:south")
  for _, r in ipairs({ { 0, 400, 1600, 40 }, { 300, 300, 160, 20 }, { 560, 220, 160, 20 } }) do
    aether.node.create("body", { x = r[1], y = r[2], w = r[3], h = r[4], color = "gray", layer = "ground", body_type = "static" })
  end
  self.player = aether.spawn("player.lua", { x = 0, y = 350 })
  aether.camera.follow(self.player, 6)
end
```
```lua
-- player.lua
local Player = { type = "body", speed = 240, jump_speed = 560, coyote = 0,
  props = { w = 28, h = 40, color = "cyan", body_type = "dynamic", layer = "player", mask = "ground" } }
function Player:update(dt)
  self.vx = (aether.input.strength("right") - aether.input.strength("left")) * self.speed
  if self.on_floor then self.coyote = 0.1 else self.coyote = self.coyote - dt end   -- coyote time
  if aether.input.pressed("jump") and self.coyote > 0 then self.vy = -self.jump_speed; self.coyote = 0 end
end
return Player
```
Platforms and walls are `static` bodies on a layer the player's mask contains. Variable jump height: when `released("jump")` and `self.vy < 0`, set `self.vy = self.vy * 0.5`.

---

## 16. Pitfalls

- **y is down**: moving up is a negative `vy`. Angles: 0° = right, 90° = down.
- **Destroyed nodes** raise an error when read or written. Store references you may outlive and check `node:is_valid()` (or `node.alive`, which works on dead nodes).
- Use `:` for methods (`node:destroy()`), `.` for functions (`aether.spawn(...)`).
- `type`, `name`, `x`, `y`, `color`, `scale`, `layer`, `mask`, `text`, `parent`, `script`... are native names: don't reuse them as your own fields.
- Frames in `animations`, and `node.frame`, are **1-based**.
- Declare `aether.physics.set_layers` **before** spawning bodies that name layers.
- Create timers with `node:every` inside scripts so they die with the node; global `aether.timer.*` timers outlive nodes until the scene changes.
- Modules (`require`) are cached across scene reloads: reset their state in `ready()`.
- Don't allocate in hot loops (`update` of hundreds of enemies): reuse tables, prefer `aether.nearest_in_group`/queries over scanning everything.
- Errors never crash the game, but they show in the console: watch it while testing.

---

## 17. Quick reference

```
aether.log/warn/error(...)  quit([code])  time()  frame()  fps()  time_scale([s])  pause(b)  is_paused()
aether.spawn(script[,props])  load_script(path)  find(name)  group(g)  group_count(g)  nearest_in_group(g,x,y[,max])
aether.node.create/build/root   aether.scene.change/reload/current   aether.screen.size/width/height
aether.timer.after/every/cancel/active   aether.tween(node,goals,dur[,opts])  cancel_tween  aether.ease.*
aether.input.bind/clear/is_down/pressed/released/strength/vector/pointer/pointer_pressed/pointer_released/touches/key_pressed/has_touch/virtual_stick/virtual_button/clear_virtual
aether.audio.play/stop/music/stop_music/volume
aether.physics.set_layers/set_gravity/gravity/set_cell_size/query_circle/query_rect/nearest/raycast
aether.camera.set_position/position/set_zoom/zoom/follow/unfollow/shake/screen_to_world/world_to_screen
aether.draw.rect/rect_outline/line/circle/text/measure_text
aether.res.preload/texture_size   aether.storage.write/read/remove/exists/list   aether.debug.colliders/stats/node_count
aether.save/load/has_save/delete_save/serialize/deserialize   aether.math.*   aether.shuffle/copy
aether.test.run/wait/wait_frames/check/inject/touch/screenshot/finish/error_count/clear_errors/quads/failures
node: destroy is_valid add_child get_child find children add_to_group remove_from_group in_group groups set set_script after every play stop_anim move distance_to angle_to direction_to look_at
callbacks: ready update fixed_update paused_update draw draw_ui on_collision_enter on_collision_exit on_animation_finished destroyed
```
