# Aether – project instructions for Claude

Aether is a lightweight, 2D-first game engine for Android (tablets first) with a planned Nintendo Switch backend.
Repo: `Imbluubyte/Aether`. Genres in focus: bullet heavens, arcade games, platformers.

## Rules (from the owner – always follow)
- Work **only on `main`**. Never create other branches.
- **Never create a README.** `CLAUDE.md` is the root doc; the user-facing doc is `docs/LUA_GUIDE.md`.
- Keep this file current: instructions, architecture, conventions, implementation status, codebase map.
- Keep `docs/LUA_GUIDE.md` current whenever the Lua API changes (new/changed function ⇒ update the guide in the same commit).
- Minimise token use: grep/targeted reads, don't re-read unchanged files, don't dump big outputs.
- Exported games must **not depend on Kotlin**: C++ engine + Lua runtime + assets/scripts must be enough.

## Architecture
```
Kotlin editor (Android app)  ──JNI──▶  C++ engine core  ◀── Lua 5.4 (embedded, compiled as C++)
Standalone player (NativeActivity, no Kotlin) ───────────▲
Future: Switch backend (libnx) ──────────────────────────┘
```
- **engine/** is platform-independent C++17. All OS access goes through injected interfaces:
  `IPlatform` (events, clock, surface size, present), `Renderer` (batch → GPU), `IAudio`, `IFileSystem` (read-only assets),
  `IStorage` (writable saves). Backends: headless/software/null (desktop tests), GLES3 + miniaudio + Android (see `android/`).
- Switch plan: new `engine/src/platform/switch/` implementing `IPlatform`/`IFileSystem`/`IStorage` over libnx (EGL+GLES3 reuses `GLESRenderer`,
  audio via `audren` or miniaudio). Gamepad buttons are named by *position* (`South/East/West/North`) so Switch A/B swap is a mapping detail.
  Don't use Android/JNI/Java types or `<android/...>` includes outside `android/` and `platform/android/`.
- Scene = tree of `Node`s in a slot pool with generational `NodeId` handles (`scene.h`). Node kinds: node, sprite, anim, body, label.
  Only `body` nodes collide. Scripts are Lua *classes* (tables) attached to nodes; instance fields live in the userdata's uservalue table.
- Physics: fixed step (default 60 Hz), uniform-grid broadphase, box/circle, layers+masks, enter/exit events, solid statics for dynamic bodies, queries/raycast.
- Frame order (`Engine::stepFrame`): poll → input → fixed_update+physics (collision callbacks) → update → timers → tweens → animations → test tick → GC dead nodes → camera → scene switch → render (nodes, `draw`, screen nodes, `draw_ui`, virtual controls, debug) → present.
- Lua errors never crash the engine: callbacks are `pcall`'d, logged with traceback, counted (`aether.test.error_count`). Lua is compiled as C++ so `luaL_error` unwinds C++ frames (destructors run) – keep it that way.
- Coroutines: every VM helper that touches the Lua stack takes the *calling* `lua_State*` (bindings may run in a coroutine). Never use `vm.L` inside a binding.

## Conventions
- C++: 4-space indent, `namespace aether`, `camelCase` functions, `PascalCase` types, trailing `_` private members. No exceptions/RTTI in engine code except through Lua.
- Lua API: `snake_case`; angles in **degrees** at the Lua boundary, radians internally; y points **down**; colours accept `{r,g,b,a}`/`0xRRGGBB`/`"#rrggbb"`/names.
- New binding ⇒ add to `bind_*.cpp`, document in `docs/LUA_GUIDE.md`, add a check to `tests/lua/api/test.lua`.
- Reading/writing a destroyed node's native property raises a Lua error (use `node:is_valid()` / `node.alive`).
- Third-party code is vendored in `engine/third_party/` (Lua 5.4.7, stb_image, miniaudio). Don't edit it.

## Build & test (desktop, headless – no GPU/audio needed)
```
cmake -S . -B build -G Ninja && cmake --build build
cd build && ctest --output-on-failure          # C++ unit tests + Lua API tests + bullet-heaven smoke test
./build/tools/aether_run/aether_run games/bullet_heaven --test tests/smoke.lua --frames 2400 --screenshot out.ppm
```
Sanitizers: configure a second dir with `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` (and the linker flag) – keep it clean.
Android (needs SDK+NDK, built in CI `.github/workflows/android.yml`): `cd android && ./gradlew :editor:assembleDebug :player:assembleDebug -PgameDir=../games/bullet_heaven`.

## Implementation status
- [x] Core: logging, file systems, timers, resources (PNG/JPG/BMP/TGA via stb), game loop, scene switching, config (`game.lua`)
- [x] Scene/nodes, groups, hierarchy, world transforms, declarative `aether.node.build`
- [x] 2D rendering: sprite batch, spritesheet frames, regions, z-order, camera (follow/zoom/shake), screen-space UI, built-in 5×7 font, immediate drawing API
- [x] Renderers: software (headless screenshots), null, GLES3 (compiled only with `AETHER_RENDERER_GLES`; untested here – no GPU)
- [x] Animation: grid spritesheet animations, loop/once, `on_animation_finished`
- [x] Input: actions, key/gamepad/axis bindings, touch, virtual stick/buttons, test injection
- [x] Physics/collisions: see above
- [x] Audio: miniaudio backend (wav/mp3/flac, sfx/music buses) + null backend (miniaudio path compiled only with `AETHER_AUDIO_MINIAUDIO`)
- [x] Timers, tweens/easing, save system (`aether.save/load`, sandboxed deserialiser), Lua test harness
- [x] Lua scripting API + guide, bullet-heaven example game, automated tests
- [ ] Android editor/player/JNI (see `android/`): written but **not yet compiled or run on a device** (no SDK/NDK in the dev sandbox)
- [ ] Switch backend (design only)
- [ ] Not started: tilemaps, particles system, text fonts beyond 5×7, scene files (scenes are Lua scripts), networking

## Codebase map
- `engine/include/aether/` public headers (types, scene, physics, input, renderer, audio, filesystem, engine, platform…)
- `engine/src/core/` engine loop, log, filesystem, timers, resources · `scene/` · `physics/` · `input/` · `render/` (batch, software, gles, font) · `audio/`
- `engine/src/script/` `lua_vm.*` (VM, callbacks, node handles), `bind_core.cpp` (aether.*, scene, timer, storage, test), `bind_node.cpp` (node props/methods, spawn/build),
  `bind_systems.cpp` (input, audio, physics, camera, draw), `prelude.cpp` (Lua-side: math, save, tween, test harness)
- `engine/src/platform/` `platform_names.cpp` (key/pad names), `android/`, `switch/` (stub)
- `tools/aether_run/` headless runner · `tests/` C++ unit tests, `tests/lua/api` Lua API tests, `tests/data` fixtures
- `games/bullet_heaven/` example game (also an integration test) · `docs/LUA_GUIDE.md` scripting guide
- `android/` Gradle project: `editor` (Kotlin app), `player` (NativeActivity, no Kotlin), `jni` shared native lib · `.github/workflows/` CI
