// Lua API for input, audio, physics, camera and immediate-mode drawing.
#include <cmath>
#include <cstring>

#include "aether/log.h"
#include "bind_util.h"

namespace aether {

static Engine& eng(lua_State* L) { return vmOf(L).engine; }

// ============================================================ input

static int in_bind(lua_State* L) {
    std::string action = luaL_checkstring(L, 1);
    for (int i = 2; i <= lua_gettop(L); i++) {
        auto add = [&](const char* b) {
            std::string err;
            if (!eng(L).input.bind(action, b, &err)) luaL_error(L, "input.bind('%s'): %s", action.c_str(), err.c_str());
        };
        if (lua_type(L, i) == LUA_TSTRING) add(lua_tostring(L, i));
        else if (lua_istable(L, i)) {
            int n = (int)luaL_len(L, i);
            for (int k = 1; k <= n; k++) {
                lua_rawgeti(L, i, k);
                if (lua_type(L, -1) == LUA_TSTRING) add(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else return luaL_error(L, "input.bind expects binding strings like 'key:space' or 'pad:south'");
    }
    return 0;
}
static int in_clear(lua_State* L) {
    if (lua_isnone(L, 1)) eng(L).input.clearAllBindings(); else eng(L).input.clearBindings(luaL_checkstring(L, 1));
    return 0;
}
static int in_isDown(lua_State* L) { lua_pushboolean(L, eng(L).input.isDown(luaL_checkstring(L, 1))); return 1; }
static int in_pressed(lua_State* L) { lua_pushboolean(L, eng(L).input.pressed(luaL_checkstring(L, 1))); return 1; }
static int in_released(lua_State* L) { lua_pushboolean(L, eng(L).input.released(luaL_checkstring(L, 1))); return 1; }
static int in_strength(lua_State* L) { lua_pushnumber(L, eng(L).input.strength(luaL_checkstring(L, 1))); return 1; }
static int in_vector(lua_State* L) {
    Vec2 v = eng(L).input.vector(luaL_checkstring(L, 1));
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    return 2;
}
static const Touch* primaryTouch(lua_State* L) {
    for (auto& t : eng(L).input.touches())
        if (t.down || t.releasedThisFrame) return &t;
    return nullptr;
}
static int in_pointer(lua_State* L) {
    const Touch* t = primaryTouch(L);
    if (!t) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushboolean(L, 0); return 3; }
    Vec2 p = eng(L).input.physicalToVirtual(t->pos);
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    lua_pushboolean(L, t->down);
    return 3;
}
static int in_pointerPressed(lua_State* L) {
    for (auto& t : eng(L).input.touches())
        if (t.pressedThisFrame) { lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, 0);
    return 1;
}
static int in_pointerReleased(lua_State* L) {
    for (auto& t : eng(L).input.touches())
        if (t.releasedThisFrame) { lua_pushboolean(L, 1); return 1; }
    lua_pushboolean(L, 0);
    return 1;
}
static int in_touches(lua_State* L) {
    const auto& ts = eng(L).input.touches();
    lua_newtable(L);
    int k = 1;
    for (auto& t : ts) {
        if (!t.down) continue;
        Vec2 p = eng(L).input.physicalToVirtual(t.pos);
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, t.id); lua_setfield(L, -2, "id");
        lua_pushnumber(L, p.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, p.y); lua_setfield(L, -2, "y");
        lua_rawseti(L, -2, k++);
    }
    return 1;
}
static int in_keyPressed(lua_State* L) {
    Key k;
    if (!keyFromName(luaL_checkstring(L, 1), k)) return luaL_error(L, "unknown key '%s'", lua_tostring(L, 1));
    lua_pushboolean(L, eng(L).input.keyPressed(k));
    return 1;
}
static int in_hasTouch(lua_State* L) {
    lua_pushboolean(L, (eng(L).svc.platform && eng(L).svc.platform->hasTouchscreen()) || eng(L).input.anyTouchSeen());
    return 1;
}

static float optField(lua_State* L, int tbl, const char* key, float def) {
    if (!lua_istable(L, tbl)) return def;
    lua_getfield(L, tbl, key);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
static std::string optStrField(lua_State* L, int tbl, const char* key, const char* def) {
    if (!lua_istable(L, tbl)) return def;
    lua_getfield(L, tbl, key);
    std::string v = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

static int in_virtualStick(lua_State* L) {
    VirtualStick s;
    s.action = luaL_checkstring(L, 1);
    s.anchor = optStrField(L, 2, "anchor", "bottom_left");
    s.dx = optField(L, 2, "x", 170);
    s.dy = optField(L, 2, "y", -170);
    s.radius = optField(L, 2, "radius", 110);
    auto& v = eng(L).input.sticks;
    for (auto& e : v)
        if (e.action == s.action) { e = s; return 0; }
    v.push_back(s);
    return 0;
}
static int in_virtualButton(lua_State* L) {
    VirtualButton b;
    b.action = luaL_checkstring(L, 1);
    b.label = optStrField(L, 2, "label", "");
    b.anchor = optStrField(L, 2, "anchor", "bottom_right");
    b.dx = optField(L, 2, "x", -150);
    b.dy = optField(L, 2, "y", -150);
    b.radius = optField(L, 2, "radius", 70);
    auto& v = eng(L).input.buttons;
    for (auto& e : v)
        if (e.action == b.action) { e = b; return 0; }
    v.push_back(b);
    return 0;
}
static int in_clearVirtual(lua_State* L) { eng(L).input.sticks.clear(); eng(L).input.buttons.clear(); return 0; }

// ============================================================ audio

static int au_play(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!eng(L).svc.audio) { lua_pushnil(L); return 1; }
    bool loop = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "loop");
        loop = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    uint32_t h = eng(L).svc.audio->playSound(path, optField(L, 2, "volume", 1), optField(L, 2, "pitch", 1), loop);
    if (h == 0) lua_pushnil(L); else lua_pushinteger(L, h);
    return 1;
}
static int au_stop(lua_State* L) {
    if (eng(L).svc.audio) eng(L).svc.audio->stopSound((uint32_t)luaL_checkinteger(L, 1));
    return 0;
}
static int au_music(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    bool loop = true;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "loop");
        if (!lua_isnil(L, -1)) loop = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    bool ok = eng(L).svc.audio && eng(L).svc.audio->playMusic(path, optField(L, 2, "volume", 1), loop);
    lua_pushboolean(L, ok);
    return 1;
}
static int au_stopMusic(lua_State* L) { if (eng(L).svc.audio) eng(L).svc.audio->stopMusic(); return 0; }
static int au_volume(lua_State* L) {
    const char* bus = luaL_optstring(L, 1, "master");
    if (!eng(L).svc.audio) { lua_pushnumber(L, 0); return 1; }
    if (!lua_isnone(L, 2)) eng(L).svc.audio->setVolume(bus, (float)luaL_checknumber(L, 2));
    lua_pushnumber(L, eng(L).svc.audio->volume(bus));
    return 1;
}

// ============================================================ physics

static uint32_t maskArg(lua_State* L, int idx) {
    if (lua_isnoneornil(L, idx)) return 0xffffffffu;
    uint32_t m;
    if (!parseLayerMask(L, idx, eng(L).physics, m)) luaL_error(L, "unknown layer (declare names with aether.physics.set_layers)");
    return m;
}
static int ph_setLayers(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    std::vector<std::string> names;
    int n = (int)luaL_len(L, 1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "set_layers expects a list of names, e.g. {'player','enemy'}");
        names.push_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    if (names.size() > 32) return luaL_error(L, "at most 32 layers are supported");
    eng(L).physics.layers.set(names);
    return 0;
}
static int ph_setGravity(lua_State* L) {
    eng(L).physics.gravity = {(float)luaL_checknumber(L, 1), (float)luaL_optnumber(L, 2, 0)};
    return 0;
}
static int ph_gravity(lua_State* L) {
    lua_pushnumber(L, eng(L).physics.gravity.x);
    lua_pushnumber(L, eng(L).physics.gravity.y);
    return 2;
}
static int ph_cellSize(lua_State* L) {
    eng(L).physics.cellSize = clampf((float)luaL_checknumber(L, 1), 8.f, 4096.f);
    eng(L).physics.invalidate();
    return 0;
}
static void pushNodes(lua_State* L, const std::vector<NodeId>& ids) {
    lua_createtable(L, (int)ids.size(), 0);
    for (size_t i = 0; i < ids.size(); i++) { vmOf(L).pushNode(L, ids[i]); lua_rawseti(L, -2, (int)i + 1); }
}
static int ph_queryCircle(lua_State* L) {
    pushNodes(L, eng(L).physics.queryCircle({(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)}, (float)luaL_checknumber(L, 3), maskArg(L, 4)));
    return 1;
}
static int ph_queryRect(lua_State* L) {
    Rect r{(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)};
    pushNodes(L, eng(L).physics.queryRect(r, maskArg(L, 5)));
    return 1;
}
static int ph_nearest(lua_State* L) {
    NodeId ignore = lua_isnoneornil(L, 5) ? kNoNode : vmOf(L).toNode(L, 5);
    NodeId id = eng(L).physics.nearest({(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)}, (float)luaL_checknumber(L, 3), maskArg(L, 4), ignore);
    if (id == kNoNode) lua_pushnil(L); else vmOf(L).pushNode(L, id);
    return 1;
}
static int ph_raycast(lua_State* L) {
    RayHit h = eng(L).physics.raycast({(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)},
                                      {(float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)}, maskArg(L, 5));
    if (!h.hit) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 6);
    vmOf(L).pushNode(L, h.node); lua_setfield(L, -2, "node");
    lua_pushnumber(L, h.point.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, h.point.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, h.normal.x); lua_setfield(L, -2, "nx");
    lua_pushnumber(L, h.normal.y); lua_setfield(L, -2, "ny");
    lua_pushnumber(L, h.distance); lua_setfield(L, -2, "distance");
    return 1;
}

// aether.nearest_in_group(group, x, y [, max_distance]) -> node, distance
static int f_nearestInGroup(lua_State* L) {
    const char* g = luaL_checkstring(L, 1);
    Vec2 p{(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)};
    float best = lua_isnoneornil(L, 4) ? 1e30f : (float)luaL_checknumber(L, 4);
    best *= best;
    NodeId bestId = kNoNode;
    for (NodeId id : eng(L).tree.group(g)) {
        const Node* n = eng(L).tree.get(id);
        if (!n || n->destroyed) continue;
        float d = (n->worldPos - p).lengthSq();
        if (d <= best) { best = d; bestId = id; }
    }
    if (bestId == kNoNode) { lua_pushnil(L); return 1; }
    vmOf(L).pushNode(L, bestId);
    lua_pushnumber(L, std::sqrt(best));
    return 2;
}

// ============================================================ camera

static int cam_setPosition(lua_State* L) {
    eng(L).camera.pos = {(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)};
    return 0;
}
static int cam_position(lua_State* L) {
    lua_pushnumber(L, eng(L).camera.pos.x);
    lua_pushnumber(L, eng(L).camera.pos.y);
    return 2;
}
static int cam_setZoom(lua_State* L) {
    eng(L).camera.zoom = clampf((float)luaL_checknumber(L, 1), 0.05f, 50.f);
    return 0;
}
static int cam_zoom(lua_State* L) { lua_pushnumber(L, eng(L).camera.zoom); return 1; }
static int cam_follow(lua_State* L) {
    NodeId id = vmOf(L).checkNode(L, 1);
    eng(L).camera.follow = id;
    eng(L).camera.smoothing = (float)luaL_optnumber(L, 2, 0);
    return 0;
}
static int cam_unfollow(lua_State* L) { (void)L; eng(L).camera.follow = kNoNode; return 0; }
static int cam_shake(lua_State* L) {
    Camera& c = eng(L).camera;
    c.shakeAmp = (float)luaL_checknumber(L, 1);
    c.shakeDur = c.shakeTime = (float)luaL_optnumber(L, 2, 0.25);
    return 0;
}
static int cam_screenToWorld(lua_State* L) {
    Vec2 w = eng(L).screenToWorld({(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)});
    lua_pushnumber(L, w.x);
    lua_pushnumber(L, w.y);
    return 2;
}
static int cam_worldToScreen(lua_State* L) {
    Vec2 s = eng(L).worldToScreen({(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)});
    lua_pushnumber(L, s.x);
    lua_pushnumber(L, s.y);
    return 2;
}

// ============================================================ immediate drawing (draw / draw_ui callbacks)

static Color colorArg(lua_State* L, int idx) {
    Color c;
    if (lua_isnoneornil(L, idx)) return c;
    if (!parseColor(L, idx, c)) luaL_error(L, "bad colour (use {r,g,b,a}, 0xRRGGBB, '#rrggbb' or a name like 'red')");
    return c;
}
static Vec2 toScreen(Engine& e, float x, float y) { return e.drawUi ? Vec2{x, y} : e.worldToScreen({x, y}); }
static float screenScale(Engine& e) { return e.drawUi ? 1.f : e.camera.zoom; }

static int dr_rect(lua_State* L) {
    Engine& e = eng(L);
    float x = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2), w = (float)luaL_checknumber(L, 3), h = (float)luaL_checknumber(L, 4);
    Vec2 p = toScreen(e, x, y);
    float s = screenScale(e);
    e.batch.axisAlignedRect(0, p.x, p.y, w * s, h * s, Rect{0, 0, 1, 1}, colorArg(L, 5));
    return 0;
}
static void segment(Engine& e, Vec2 a, Vec2 b, float thick, Color c) {
    e.batch.line(toScreen(e, a.x, a.y), toScreen(e, b.x, b.y), thick * screenScale(e), c);
}
static int dr_rectOutline(lua_State* L) {
    Engine& e = eng(L);
    float x = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2), w = (float)luaL_checknumber(L, 3), h = (float)luaL_checknumber(L, 4);
    Color c = colorArg(L, 5);
    float t = (float)luaL_optnumber(L, 6, 1);
    segment(e, {x, y}, {x + w, y}, t, c);
    segment(e, {x + w, y}, {x + w, y + h}, t, c);
    segment(e, {x + w, y + h}, {x, y + h}, t, c);
    segment(e, {x, y + h}, {x, y}, t, c);
    return 0;
}
static int dr_line(lua_State* L) {
    segment(eng(L), {(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)}, {(float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)},
            (float)luaL_optnumber(L, 6, 1), colorArg(L, 5));
    return 0;
}
static int dr_circle(lua_State* L) {
    Engine& e = eng(L);
    float cx = (float)luaL_checknumber(L, 1), cy = (float)luaL_checknumber(L, 2), r = (float)luaL_checknumber(L, 3);
    Color c = colorArg(L, 4);
    float t = (float)luaL_optnumber(L, 5, 1);
    int segs = r < 12 ? 12 : (r < 60 ? 24 : 40);
    Vec2 prev{cx + r, cy};
    for (int i = 1; i <= segs; i++) {
        float a = (float)i / segs * 2 * kPi;
        Vec2 cur{cx + std::cos(a) * r, cy + std::sin(a) * r};
        segment(e, prev, cur, t, c);
        prev = cur;
    }
    return 0;
}
static int dr_text(lua_State* L) {
    Engine& e = eng(L);
    const char* s = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    float size = optField(L, 4, "size", 2);
    Color c;
    if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "color");
        if (!lua_isnil(L, -1) && !parseColor(L, -1, c)) luaL_error(L, "bad text colour");
        lua_pop(L, 1);
    }
    std::string al = optStrField(L, 4, "align", "left");
    Vec2 p = toScreen(e, x, y);
    e.font.draw(e.batch, s, p.x, p.y, size * screenScale(e), c, al == "center" ? 1 : (al == "right" ? 2 : 0));
    return 0;
}
static int dr_measure(lua_State* L) {
    Vec2 m = eng(L).font.measure(luaL_checkstring(L, 1), (float)luaL_optnumber(L, 2, 2));
    lua_pushnumber(L, m.x);
    lua_pushnumber(L, m.y);
    return 2;
}

static void setFuncs(lua_State* L, const char* sub, const luaL_Reg* regs) {
    lua_getglobal(L, "aether");
    if (sub) {
        lua_newtable(L);
        luaL_setfuncs(L, regs, 0);
        lua_setfield(L, -2, sub);
    } else {
        luaL_setfuncs(L, regs, 0);
    }
    lua_pop(L, 1);
}

void bindSystems(lua_State* L) {
    static const luaL_Reg input[] = {{"bind", in_bind}, {"clear", in_clear}, {"is_down", in_isDown}, {"pressed", in_pressed}, {"released", in_released},
                                     {"strength", in_strength}, {"vector", in_vector}, {"pointer", in_pointer}, {"pointer_pressed", in_pointerPressed},
                                     {"pointer_released", in_pointerReleased}, {"touches", in_touches}, {"key_pressed", in_keyPressed},
                                     {"has_touch", in_hasTouch}, {"virtual_stick", in_virtualStick}, {"virtual_button", in_virtualButton},
                                     {"clear_virtual", in_clearVirtual}, {nullptr, nullptr}};
    static const luaL_Reg audio[] = {{"play", au_play}, {"stop", au_stop}, {"music", au_music}, {"stop_music", au_stopMusic}, {"volume", au_volume}, {nullptr, nullptr}};
    static const luaL_Reg physics[] = {{"set_layers", ph_setLayers}, {"set_gravity", ph_setGravity}, {"gravity", ph_gravity}, {"set_cell_size", ph_cellSize},
                                       {"query_circle", ph_queryCircle}, {"query_rect", ph_queryRect}, {"nearest", ph_nearest}, {"raycast", ph_raycast},
                                       {nullptr, nullptr}};
    static const luaL_Reg camera[] = {{"set_position", cam_setPosition}, {"position", cam_position}, {"set_zoom", cam_setZoom}, {"zoom", cam_zoom},
                                      {"follow", cam_follow}, {"unfollow", cam_unfollow}, {"shake", cam_shake}, {"screen_to_world", cam_screenToWorld},
                                      {"world_to_screen", cam_worldToScreen}, {nullptr, nullptr}};
    static const luaL_Reg draw[] = {{"rect", dr_rect}, {"rect_outline", dr_rectOutline}, {"line", dr_line}, {"circle", dr_circle}, {"text", dr_text},
                                    {"measure_text", dr_measure}, {nullptr, nullptr}};
    static const luaL_Reg extra[] = {{"nearest_in_group", f_nearestInGroup}, {nullptr, nullptr}};
    setFuncs(L, "input", input);
    setFuncs(L, "audio", audio);
    setFuncs(L, "physics", physics);
    setFuncs(L, "camera", camera);
    setFuncs(L, "draw", draw);
    setFuncs(L, nullptr, extra);
}

}  // namespace aether
