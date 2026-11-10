// Core Lua API: logging, time, screen, scenes, spawning, timers, resources, storage, debug and test hooks.
#include "aether/log.h"
#include "bind_util.h"

namespace aether {

static Engine& eng(lua_State* L) { return vmOf(L).engine; }

static std::string joinArgs(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += ' ';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}

static int f_log(lua_State* L) { log::write(LogLevel::Info, joinArgs(L)); return 0; }
static int f_warn(lua_State* L) { log::write(LogLevel::Warn, joinArgs(L)); return 0; }
static int f_error(lua_State* L) { log::write(LogLevel::Error, joinArgs(L)); return 0; }
static int f_traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "error", 1);
    return 1;
}
static int f_quit(lua_State* L) { eng(L).requestQuit((int)luaL_optinteger(L, 1, 0)); return 0; }
static int f_time(lua_State* L) { lua_pushnumber(L, eng(L).time); return 1; }
static int f_frame(lua_State* L) { lua_pushinteger(L, (lua_Integer)eng(L).frameCount); return 1; }
static int f_fps(lua_State* L) { lua_pushnumber(L, eng(L).fps); return 1; }
static int f_timeScale(lua_State* L) {
    if (!lua_isnone(L, 1)) eng(L).timeScale = clampf((float)luaL_checknumber(L, 1), 0.f, 20.f);
    lua_pushnumber(L, eng(L).timeScale);
    return 1;
}
static int f_pause(lua_State* L) {
    eng(L).paused = lua_toboolean(L, 1) != 0;
    if (eng(L).svc.audio) eng(L).svc.audio->pauseAll(eng(L).paused);
    return 0;
}
static int f_isPaused(lua_State* L) { lua_pushboolean(L, eng(L).paused); return 1; }

static int screen_size(lua_State* L) { lua_pushnumber(L, eng(L).virtW); lua_pushnumber(L, eng(L).virtH); return 2; }
static int screen_width(lua_State* L) { lua_pushnumber(L, eng(L).virtW); return 1; }
static int screen_height(lua_State* L) { lua_pushnumber(L, eng(L).virtH); return 1; }

static int f_loadScript(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    if (!vmOf(L).pushClass(L, path)) return luaL_error(L, "load_script: could not load '%s' (see log)", path);
    return 1;
}
static int f_spawn(lua_State* L) { return nodeSpawn(L, 1, lua_istable(L, 2) ? 2 : 0); }

static int node_create(lua_State* L) {
    NodeType t;
    const char* ts = luaL_optstring(L, 1, "node");
    if (!parseNodeType(ts, t)) return luaL_error(L, "unknown node type '%s' (use node, sprite, anim, body or label)", ts);
    NodeId id = nodeCreate(L, t, lua_istable(L, 2) ? 2 : 0);
    vmOf(L).pushNode(L, id);
    return 1;
}
static int node_build(lua_State* L) { return nodeBuild(L, 1); }
static int node_root(lua_State* L) { vmOf(L).pushNode(L, eng(L).tree.root()); return 1; }

static int f_find(lua_State* L) {
    NodeId id = eng(L).tree.findChild(eng(L).tree.root(), luaL_checkstring(L, 1), true);
    if (id == kNoNode) lua_pushnil(L); else vmOf(L).pushNode(L, id);
    return 1;
}
static int f_group(lua_State* L) {
    std::vector<NodeId> ids = eng(L).tree.group(luaL_checkstring(L, 1));
    lua_createtable(L, (int)ids.size(), 0);
    for (size_t i = 0; i < ids.size(); i++) { vmOf(L).pushNode(L, ids[i]); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}
static int f_groupCount(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)eng(L).tree.groupCount(luaL_checkstring(L, 1)));
    return 1;
}

static int scene_change(lua_State* L) { eng(L).pendingScene = luaL_checkstring(L, 1); return 0; }
static int scene_reload(lua_State* L) { (void)L; eng(L).pendingScene = eng(L).scenePath; return 0; }
static int scene_current(lua_State* L) { lua_pushstring(L, eng(L).scenePath.c_str()); return 1; }

static int timer_after(lua_State* L) {
    lua_pushinteger(L, timerAdd(L, kNoNode, 2, (float)luaL_checknumber(L, 1), 0, 0));
    return 1;
}
static int timer_every(lua_State* L) {
    float sec = (float)luaL_checknumber(L, 1);
    if (sec <= 0) return luaL_error(L, "every() needs an interval greater than 0");
    lua_pushinteger(L, timerAdd(L, kNoNode, 2, sec, sec, (int)luaL_optinteger(L, 3, -1)));
    return 1;
}
static int timer_cancel(lua_State* L) { lua_pushboolean(L, eng(L).timers.cancel((int)luaL_checkinteger(L, 1))); return 1; }
static int timer_active(lua_State* L) { lua_pushboolean(L, eng(L).timers.active((int)luaL_checkinteger(L, 1))); return 1; }

static int res_preload(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int n = (int)luaL_len(L, 1);
    int failed = 0;
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        if (lua_type(L, -1) == LUA_TSTRING && eng(L).resources.texture(lua_tostring(L, -1)).failed) failed++;
        lua_pop(L, 1);
    }
    lua_pushinteger(L, failed);
    return 1;
}
static int res_textureSize(lua_State* L) {
    const TextureInfo& t = eng(L).resources.texture(luaL_checkstring(L, 1));
    if (t.failed) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, t.w);
    lua_pushinteger(L, t.h);
    return 2;
}

static IStorage* storage(lua_State* L) {
    IStorage* s = eng(L).svc.storage.get();
    if (!s) luaL_error(L, "storage is not available on this platform");
    return s;
}
static int st_write(lua_State* L) {
    size_t len;
    const char* name = luaL_checkstring(L, 1);
    const char* data = luaL_checklstring(L, 2, &len);
    lua_pushboolean(L, storage(L)->write(name, std::string(data, len)));
    return 1;
}
static int st_read(lua_State* L) {
    std::string out;
    if (!storage(L)->read(luaL_checkstring(L, 1), out)) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}
static int st_remove(lua_State* L) { lua_pushboolean(L, storage(L)->remove(luaL_checkstring(L, 1))); return 1; }
static int st_exists(lua_State* L) { lua_pushboolean(L, storage(L)->exists(luaL_checkstring(L, 1))); return 1; }
static int st_list(lua_State* L) {
    auto names = storage(L)->list();
    lua_createtable(L, (int)names.size(), 0);
    for (size_t i = 0; i < names.size(); i++) { lua_pushstring(L, names[i].c_str()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}

static int dbg_colliders(lua_State* L) { eng(L).debugColliders = lua_toboolean(L, 1) != 0; return 0; }
static int dbg_stats(lua_State* L) { eng(L).debugStats = lua_toboolean(L, 1) != 0; return 0; }
static int dbg_nodeCount(lua_State* L) { lua_pushinteger(L, (lua_Integer)eng(L).tree.liveCount()); return 1; }

// ---- test hooks (also usable from the editor's play view)
static int test_inject(lua_State* L) { eng(L).input.inject(luaL_checkstring(L, 1), (float)luaL_optnumber(L, 2, 1.0)); return 0; }
static int test_touch(lua_State* L) {
    eng(L).input.injectTouch((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}
static int test_screenshot(lua_State* L) { lua_pushboolean(L, eng(L).savePPM(luaL_checkstring(L, 1))); return 1; }
static int test_finish(lua_State* L) { eng(L).requestQuit((int)luaL_optinteger(L, 1, 0)); return 0; }
static int test_errorCount(lua_State* L) { lua_pushinteger(L, vmOf(L).errorCount); return 1; }
static int test_clearErrors(lua_State* L) { vmOf(L).errorCount = 0; vmOf(L).errorMessages.clear(); return 0; }
static int test_quads(lua_State* L) {
    lua_pushinteger(L, eng(L).svc.renderer ? (lua_Integer)eng(L).svc.renderer->lastFrameQuads() : 0);
    return 1;
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

void bindCore(lua_State* L) {
    static const luaL_Reg core[] = {{"log", f_log}, {"_traceback", f_traceback}, {"warn", f_warn}, {"error", f_error}, {"quit", f_quit}, {"time", f_time},
                                    {"frame", f_frame}, {"fps", f_fps}, {"time_scale", f_timeScale}, {"pause", f_pause},
                                    {"is_paused", f_isPaused}, {"load_script", f_loadScript}, {"spawn", f_spawn}, {"find", f_find},
                                    {"group", f_group}, {"group_count", f_groupCount}, {nullptr, nullptr}};
    static const luaL_Reg screen[] = {{"size", screen_size}, {"width", screen_width}, {"height", screen_height}, {nullptr, nullptr}};
    static const luaL_Reg node[] = {{"create", node_create}, {"build", node_build}, {"root", node_root}, {nullptr, nullptr}};
    static const luaL_Reg scene[] = {{"change", scene_change}, {"reload", scene_reload}, {"current", scene_current}, {nullptr, nullptr}};
    static const luaL_Reg timer[] = {{"after", timer_after}, {"every", timer_every}, {"cancel", timer_cancel}, {"active", timer_active}, {nullptr, nullptr}};
    static const luaL_Reg res[] = {{"preload", res_preload}, {"texture_size", res_textureSize}, {nullptr, nullptr}};
    static const luaL_Reg stor[] = {{"write", st_write}, {"read", st_read}, {"remove", st_remove}, {"exists", st_exists}, {"list", st_list}, {nullptr, nullptr}};
    static const luaL_Reg dbg[] = {{"colliders", dbg_colliders}, {"stats", dbg_stats}, {"node_count", dbg_nodeCount}, {nullptr, nullptr}};
    static const luaL_Reg test[] = {{"inject", test_inject}, {"touch", test_touch}, {"screenshot", test_screenshot}, {"finish", test_finish},
                                    {"error_count", test_errorCount}, {"clear_errors", test_clearErrors}, {"quads", test_quads}, {nullptr, nullptr}};
    setFuncs(L, nullptr, core);
    setFuncs(L, "screen", screen);
    setFuncs(L, "node", node);
    setFuncs(L, "scene", scene);
    setFuncs(L, "timer", timer);
    setFuncs(L, "res", res);
    setFuncs(L, "storage", stor);
    setFuncs(L, "debug", dbg);
    setFuncs(L, "test", test);
}

}  // namespace aether
