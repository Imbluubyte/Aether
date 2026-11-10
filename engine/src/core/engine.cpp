#include "aether/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../script/lua_vm.h"
#include "aether/log.h"

namespace aether {

Engine::Engine(Services s) : svc(std::move(s)), physics(tree) {}

Engine::~Engine() { shutdown(); }

int Engine::scriptErrors() const { return vm ? vm->errorCount : 0; }

// ------------------------------------------------------------------ config (game.lua)

static float cfgNum(lua_State* L, const char* key, float def) {
    lua_getfield(L, -1, key);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

bool Engine::loadConfig() {
    std::string code;
    if (!svc.assets || !svc.assets->readText("game.lua", code)) return true;  // optional
    lua_State* L = luaL_newstate();
    bool ok = false;
    if (luaL_loadbuffer(L, code.data(), code.size(), "@game.lua") == LUA_OK && lua_pcall(L, 0, 1, 0) == LUA_OK && lua_istable(L, -1)) {
        ok = true;
        lua_getfield(L, -1, "title");
        if (lua_type(L, -1) == LUA_TSTRING) config.title = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "main");
        if (lua_type(L, -1) == LUA_TSTRING) config.mainScript = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "stretch");
        if (lua_type(L, -1) == LUA_TSTRING) config.stretch = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "smooth");
        if (lua_isboolean(L, -1)) config.smooth = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        config.width = (int)cfgNum(L, "width", (float)config.width);
        config.height = (int)cfgNum(L, "height", (float)config.height);
        config.fixedHz = clampf(cfgNum(L, "fixed_hz", config.fixedHz), 10.f, 240.f);
        lua_getfield(L, -1, "background");
        if (lua_istable(L, -1)) {
            float c[3] = {config.background.r, config.background.g, config.background.b};
            for (int i = 0; i < 3; i++) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_isnumber(L, -1)) c[i] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
            config.background = Color(c[0], c[1], c[2], 1);
        }
        lua_pop(L, 1);
    } else {
        log::errorf("game.lua is invalid: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "must return a table");
    }
    lua_close(L);
    config.width = std::max(config.width, 16);
    config.height = std::max(config.height, 16);
    return ok;
}

// ------------------------------------------------------------------ lifecycle

static void uploadFont(Engine& e) {
    std::vector<uint8_t> px;
    int w, h;
    Font::buildAtlas(px, w, h);
    e.font.setTexture(e.svc.renderer ? e.svc.renderer->createTexture(w, h, px.data(), false) : 0);
}

bool Engine::start(const std::string& testScript) {
    loadConfig();
    testScript_ = testScript;
    testMode = !testScript.empty();
    virtW = (float)config.width;
    virtH = (float)config.height;
    if (svc.renderer && !svc.renderer->init()) {
        log::errorf("renderer failed to initialise");
        return false;
    }
    uploadFont(*this);
    resources.configure(svc.assets.get(), svc.renderer.get(), config.smooth);
    if (svc.audio) svc.audio->init(svc.assets.get());
    physics.fixedStep = 1.f / config.fixedHz;

    vm = std::make_unique<LuaVM>(*this);
    if (!vm->init()) return false;
    running_ = true;
    quit_ = false;
    if (!vm->loadScene(config.mainScript)) {
        log::errorf("could not start: failed to load main script '%s'", config.mainScript.c_str());
        running_ = false;
        return false;
    }
    if (!testScript.empty() && !vm->runTestScript(testScript)) {
        running_ = false;
        return false;
    }
    lastPlatformTime_ = svc.platform ? svc.platform->timeSeconds() : 0;
    return true;
}

bool Engine::restart() {
    if (vm) {
        timers.clear();
        vm.reset();
    }
    tree.reset();
    physics.reset();
    physics.gravity = {0, 0};
    physics.layers.set({});
    input = Input();
    camera = Camera();
    batch.clear();
    time = 0;
    frameCount = 0;
    timeScale = 1;
    paused = false;
    pendingScene.clear();
    scenePath.clear();
    fixedAccum_ = 0;
    resources.clear();
    if (svc.audio) { svc.audio->stopMusic(); svc.audio->pauseAll(false); }
    resources.configure(svc.assets.get(), svc.renderer.get(), config.smooth);
    config = GameConfig();
    loadConfig();
    virtW = (float)config.width;
    virtH = (float)config.height;
    physics.fixedStep = 1.f / config.fixedHz;
    vm = std::make_unique<LuaVM>(*this);
    if (!vm->init()) return false;
    quit_ = false;
    running_ = true;
    bool ok = vm->loadScene(config.mainScript);
    if (!testScript_.empty()) vm->runTestScript(testScript_);
    lastPlatformTime_ = svc.platform ? svc.platform->timeSeconds() : 0;
    return ok;
}

void Engine::shutdown() {
    if (!running_ && !vm) return;
    running_ = false;
    vm.reset();
    resources.clear();
    if (svc.audio) svc.audio->shutdown();
    if (svc.renderer) svc.renderer->shutdown();
}

void Engine::onSurfaceLost() {
    resources.forget();
    font.setTexture(0);
    // Nodes keep their path; textures are re-resolved lazily after the surface returns.
    tree.traverse([](Node& n) { n.texture = 0; });
    if (svc.renderer) svc.renderer->contextLost();
}

void Engine::onSurfaceReady() {
    if (!svc.renderer) return;
    svc.renderer->init();
    uploadFont(*this);
    resources.configure(svc.assets.get(), svc.renderer.get(), config.smooth);
}

// ------------------------------------------------------------------ frame

void Engine::frame() {
    double now = svc.platform->timeSeconds();
    double dt = lastPlatformTime_ < 0 ? 1.0 / 60 : now - lastPlatformTime_;
    lastPlatformTime_ = now;
    stepFrame(clampf((float)dt, 0.f, 0.1f));
}

void Engine::updateAnimations(float dt) {
    struct Done { NodeId id; std::string name; };
    std::vector<Done> done;
    tree.traverse([&](Node& n) {
        if (!n.animPlaying || n.animIndex < 0 || n.animIndex >= (int)n.animations.size()) return;
        const Animation& a = n.animations[(size_t)n.animIndex];
        float frameDur = 1.f / std::max(a.fps, 0.01f);
        n.animTime += dt * n.animSpeed;
        while (n.animTime >= frameDur) {
            n.animTime -= frameDur;
            n.animFrame++;
            if (n.animFrame >= (int)a.frames.size()) {
                if (a.loop) n.animFrame = 0;
                else { n.animFrame = (int)a.frames.size() - 1; n.animPlaying = false; done.push_back({n.id, a.name}); n.animTime = 0; break; }
            }
        }
        n.cell = a.frames[(size_t)n.animFrame];
    });
    for (auto& d : done) vm->animationFinished(d.id, d.name);
}

void Engine::updateCamera(float dt) {
    if (const Node* f = tree.get(camera.follow)) {
        if (camera.smoothing <= 0) camera.pos = f->worldPos;
        else camera.pos += (f->worldPos - camera.pos) * (1.f - std::exp(-camera.smoothing * dt));
    }
    if (camera.shakeTime > 0) {
        camera.shakeTime -= dt;
        float k = camera.shakeDur > 0 ? std::max(0.f, camera.shakeTime / camera.shakeDur) : 0;
        float a = camera.shakeAmp * k;
        camera.shakeOffset = {((float)std::rand() / RAND_MAX * 2 - 1) * a, ((float)std::rand() / RAND_MAX * 2 - 1) * a};
    } else {
        camera.shakeOffset = {0, 0};
    }
}

void Engine::applyPendingScene() {
    if (pendingScene.empty()) return;
    std::string path = pendingScene;
    pendingScene.clear();
    NodeId root = tree.root();
    if (const Node* r = tree.get(root)) {
        std::vector<NodeId> kids = r->children;
        for (NodeId c : kids) vm->destroyNode(c);
    }
    vm->clearRootScript();
    timers.clear();
    vm->releaseRefs(timers.takeReleased());
    int top = lua_gettop(vm->L);
    lua_getglobal(vm->L, "aether");
    lua_getfield(vm->L, -1, "_reset_tweens");
    if (lua_isfunction(vm->L, -1)) vm->call(0, 0, "reset_tweens");
    lua_settop(vm->L, top);
    vm->nodesFreed(tree.collectGarbage());
    physics.reset();
    input.sticks.clear();
    input.buttons.clear();
    camera = Camera();
    vm->loadScene(path);
}

void Engine::stepFrame(double dtIn) {
    if (!running()) return;
    float dt = (float)dtIn;

    events_.clear();
    if (svc.platform) svc.platform->pollEvents(events_);
    for (const Event& e : events_) {
        if (e.type == Event::Quit) requestQuit();
        else if (e.type == Event::Pause && svc.audio) svc.audio->pauseAll(true);
        else if (e.type == Event::Resume && svc.audio) svc.audio->pauseAll(paused);
    }
    int vx, vy, vw, vh;
    computeView(vx, vy, vw, vh);
    input.process(events_);
    input.update(svc.platform ? svc.platform->width() : vw, svc.platform ? svc.platform->height() : vh, virtW, virtH);
    vm->errorsThisFrame = 0;

    // fps
    fpsAccum_ += dt;
    fpsFrames_++;
    if (fpsAccum_ >= 0.5f) { fps = (float)fpsFrames_ / fpsAccum_; fpsAccum_ = 0; fpsFrames_ = 0; }

    tree.updateTransforms();
    if (!paused) {
        float sdt = dt * timeScale;
        time += sdt;
        fixedAccum_ += sdt;
        int steps = 0;
        while (fixedAccum_ >= physics.fixedStep && steps < 5) {
            fixedAccum_ -= physics.fixedStep;
            steps++;
            vm->fixedUpdate(physics.fixedStep);
            tree.updateTransforms();
            physics.step(physics.fixedStep, [&](NodeId self, NodeId other, bool enter) { vm->collision(self, other, enter); });
        }
        if (steps == 5) fixedAccum_ = 0;  // spiral-of-death guard
        vm->update(sdt);
        vm->fireTimers(sdt);
        vm->updateTweens(sdt);
        updateAnimations(sdt);
    } else {
        vm->update(dt, "paused_update");
    }
    vm->testTick(dt);
    vm->nodesFreed(tree.collectGarbage());
    physics.invalidate();
    updateCamera(dt);
    applyPendingScene();
    tree.updateTransforms();
    frameCount++;

    render();
    if (svc.audio) svc.audio->update();
    if (svc.platform) {
        svc.platform->present();
        if (svc.platform->shouldQuit()) requestQuit();
    }
}

// ------------------------------------------------------------------ view + camera maths

void Engine::computeView(int& vpX, int& vpY, int& vpW, int& vpH) {
    int fbW = svc.platform ? svc.platform->width() : config.width;
    int fbH = svc.platform ? svc.platform->height() : config.height;
    fbW = std::max(fbW, 1);
    fbH = std::max(fbH, 1);
    if (config.stretch == "fit") {
        virtW = (float)config.width;
        virtH = (float)config.height;
        float s = std::min((float)fbW / virtW, (float)fbH / virtH);
        vpW = (int)(virtW * s);
        vpH = (int)(virtH * s);
        vpX = (fbW - vpW) / 2;
        vpY = (fbH - vpH) / 2;
        viewScale_ = s;
    } else {  // expand: keep the design height, extend the width to match the screen aspect
        virtH = (float)config.height;
        virtW = std::max(virtH * (float)fbW / (float)fbH, 16.f);
        vpX = vpY = 0;
        vpW = fbW;
        vpH = fbH;
        viewScale_ = (float)fbH / virtH;
    }
    vpX_ = vpX; vpY_ = vpY; vpW_ = vpW; vpH_ = vpH;
    input.setViewMapping((float)vpX, (float)vpY, viewScale_);
    if (svc.renderer) svc.renderer->setView(fbW, fbH, vpX, vpY, vpW, vpH, virtW, virtH);
}

Vec2 Engine::worldToScreen(Vec2 w) const {
    return {(w.x - camera.pos.x - camera.shakeOffset.x) * camera.zoom + virtW * 0.5f, (w.y - camera.pos.y - camera.shakeOffset.y) * camera.zoom + virtH * 0.5f};
}

Vec2 Engine::screenToWorld(Vec2 s) const {
    return {(s.x - virtW * 0.5f) / camera.zoom + camera.pos.x + camera.shakeOffset.x, (s.y - virtH * 0.5f) / camera.zoom + camera.pos.y + camera.shakeOffset.y};
}

// ------------------------------------------------------------------ rendering

void Engine::drawNodes(bool screenSpacePass) {
    struct Item { int z; Node* n; };
    static thread_local std::vector<Item> items;
    items.clear();
    struct Frame { NodeId id; bool screen; };
    std::vector<Frame> stack{{tree.root(), false}};
    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        Node* n = tree.get(f.id);
        if (!n || n->destroyed || !n->visible) continue;
        bool screen = f.screen || n->screenSpace;
        if (screen == screenSpacePass && n->type != NodeType::Node) items.push_back({n->z, n});
        for (size_t i = n->children.size(); i-- > 0;) stack.push_back({n->children[i], screen});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.z < b.z; });

    const float zoom = screenSpacePass ? 1.f : camera.zoom;
    for (const Item& it : items) {
        Node& n = *it.n;
        Vec2 sp = screenSpacePass ? n.worldPos : worldToScreen(n.worldPos);
        if (n.type == NodeType::Label) {
            Color c = n.hasColor ? n.color : Color();
            font.draw(batch, n.text, sp.x, sp.y, n.textSize * zoom * std::fabs(n.worldScale.x), c, n.align);
            continue;
        }
        if (n.texture == 0 && !n.texturePath.empty()) {
            const TextureInfo& t = resources.texture(n.texturePath);
            n.texture = t.id; n.texW = t.w; n.texH = t.h;
        }
        bool missing = !n.texturePath.empty() && n.texture == 0;
        if (n.type == NodeType::Body && !n.hasColor && n.texture == 0 && !missing) continue;  // collider only
        Vec2 base = n.baseSize();
        if (missing && base.x <= 0) base = {32, 32};
        float sw = base.x * std::fabs(n.worldScale.x) * zoom, sh = base.y * std::fabs(n.worldScale.y) * zoom;
        if (sw <= 0 || sh <= 0) continue;
        float cull = std::max(sw, sh);
        if (sp.x < -cull || sp.y < -cull || sp.x > virtW + cull || sp.y > virtH + cull) continue;

        Rect uv{0, 0, 1, 1};
        if (n.texture && n.texW > 0 && n.texH > 0) {
            if (n.frameW > 0 && n.frameH > 0) {
                int cols = std::max(1, n.texW / n.frameW);
                float cx = (float)(n.cell % cols) * n.frameW, cy = (float)(n.cell / cols) * n.frameH;
                uv = {cx / n.texW, cy / n.texH, (float)n.frameW / n.texW, (float)n.frameH / n.texH};
            } else if (n.region.w > 0 && n.region.h > 0) {
                uv = {n.region.x / n.texW, n.region.y / n.texH, n.region.w / n.texW, n.region.h / n.texH};
            }
        }
        bool fx = n.flipX != (n.worldScale.x < 0), fy = n.flipY != (n.worldScale.y < 0);
        if (fx) { uv.x += uv.w; uv.w = -uv.w; }
        if (fy) { uv.y += uv.h; uv.h = -uv.h; }

        float c = std::cos(n.worldRot), s = std::sin(n.worldRot);
        float ox = n.origin.x * sw, oy = n.origin.y * sh;
        const float lx[4] = {-ox, sw - ox, sw - ox, -ox}, ly[4] = {-oy, -oy, sh - oy, sh - oy};
        Vec2 corners[4];
        for (int i = 0; i < 4; i++) corners[i] = {sp.x + lx[i] * c - ly[i] * s, sp.y + lx[i] * s + ly[i] * c};
        Color col = missing ? Color(1, 0, 1, 1) : (n.hasColor ? n.color : Color());
        batch.quad(missing ? 0 : n.texture, corners, uv, col);
    }
}

void Engine::drawCircle(Vec2 c, float r, float t, Color col) {
    int segs = r < 12 ? 12 : (r < 60 ? 24 : 40);
    Vec2 prev{c.x + r, c.y};
    for (int i = 1; i <= segs; i++) {
        float a = (float)i / segs * 2 * kPi;
        Vec2 cur{c.x + std::cos(a) * r, c.y + std::sin(a) * r};
        batch.line(prev, cur, t, col);
        prev = cur;
    }
}

void Engine::drawVirtualControls() {
    bool touch = (svc.platform && svc.platform->hasTouchscreen()) || input.anyTouchSeen();
    if (!touch) return;
    for (const auto& s : input.sticks) {
        Vec2 c = input.anchorPos(s.anchor, s.dx, s.dy, virtW, virtH);
        drawCircle(c, s.radius, 3, Color(1, 1, 1, 0.35f));
        Vec2 k = c + s.knob * s.radius;
        batch.axisAlignedRect(0, k.x - 28, k.y - 28, 56, 56, Rect{0, 0, 1, 1}, Color(1, 1, 1, s.touchId >= 0 ? 0.5f : 0.25f));
    }
    for (const auto& b : input.buttons) {
        Vec2 c = input.anchorPos(b.anchor, b.dx, b.dy, virtW, virtH);
        drawCircle(c, b.radius, 3, Color(1, 1, 1, b.touchId >= 0 ? 0.7f : 0.35f));
        if (!b.label.empty()) font.draw(batch, b.label, c.x, c.y - 7, 2, Color(1, 1, 1, 0.6f), 1);
    }
}

void Engine::drawDebug() {
    if (debugColliders) {
        tree.traverse([&](Node& n) {
            if (n.type != NodeType::Body || !n.bodyActive) return;
            Vec2 size = n.baseSize();
            Vec2 p = worldToScreen(n.worldPos);
            Color col = n.body == BodyType::Static ? Color(0.3f, 1, 0.3f, 0.8f) : (n.body == BodyType::Trigger ? Color(1, 1, 0.2f, 0.8f) : Color(1, 0.4f, 0.2f, 0.8f));
            if (n.circle || n.radius > 0) {
                float r = (n.radius > 0 ? n.radius : std::min(size.x, size.y) * 0.5f) * std::max(std::fabs(n.worldScale.x), std::fabs(n.worldScale.y)) * camera.zoom;
                drawCircle(p, r, 1.5f, col);
            } else {
                float hw = size.x * std::fabs(n.worldScale.x) * camera.zoom * 0.5f, hh = size.y * std::fabs(n.worldScale.y) * camera.zoom * 0.5f;
                Vec2 a{p.x - hw, p.y - hh}, b{p.x + hw, p.y - hh}, c{p.x + hw, p.y + hh}, d{p.x - hw, p.y + hh};
                batch.line(a, b, 1.5f, col); batch.line(b, c, 1.5f, col); batch.line(c, d, 1.5f, col); batch.line(d, a, 1.5f, col);
            }
        });
    }
    if (debugStats) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%.0f fps  nodes %zu  bodies %zu  timers %zu  tex %zu", fps, tree.liveCount(), physics.bodyCount(), timers.count(), resources.textureCount());
        batch.axisAlignedRect(0, 4, 4, 8.f + 12.f * (float)std::char_traits<char>::length(buf), 22, Rect{0, 0, 1, 1}, Color(0, 0, 0, 0.6f));
        font.draw(batch, buf, 8, 8, 2, Color(0.6f, 1, 0.6f, 1), 0);
    }
}

void Engine::render() {
    if (!svc.renderer) return;
    batch.clear();
    svc.renderer->beginFrame(config.background);
    drawUi = false;
    drawNodes(false);
    vm->draw(false);
    drawNodes(true);
    drawUi = true;
    vm->draw(true);
    drawUi = false;
    drawVirtualControls();
    drawDebug();
    svc.renderer->draw(batch);
    svc.renderer->endFrame();
}

bool Engine::savePPM(const std::string& path) const {
    int w, h;
    std::vector<uint8_t> px;
    if (!svc.renderer || !svc.renderer->readPixels(w, h, px) || w <= 0 || h <= 0) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (size_t i = 0; i + 3 < px.size(); i += 4) std::fwrite(&px[i], 1, 3, f);
    std::fclose(f);
    return true;
}

}  // namespace aether
