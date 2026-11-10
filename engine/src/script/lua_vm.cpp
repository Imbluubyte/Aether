#include "lua_vm.h"

#include <algorithm>
#include <unordered_set>

#include "aether/log.h"

namespace aether {

LuaVM& vmOf(lua_State* L) { return **static_cast<LuaVM**>(lua_getextraspace(L)); }

LuaVM::LuaVM(Engine& e) : engine(e) {}

LuaVM::~LuaVM() {
    if (L) lua_close(L);
}

// ------------------------------------------------------------------ require() over the asset FS

static int searcher(lua_State* L) {
    std::string mod = luaL_checkstring(L, 1);
    std::string rel = mod;
    std::replace(rel.begin(), rel.end(), '.', '/');
    LuaVM& vm = vmOf(L);
    std::string tried;
    for (const std::string& cand : {rel + ".lua", rel + "/init.lua"}) {
        std::string code;
        if (vm.engine.svc.assets && vm.engine.svc.assets->readText(cand, code)) {
            if (luaL_loadbuffer(L, code.data(), code.size(), ("@" + cand).c_str()) != LUA_OK) {
                return luaL_error(L, "error loading module '%s' from '%s':\n\t%s", mod.c_str(), cand.c_str(), lua_tostring(L, -1));
            }
            lua_pushstring(L, cand.c_str());
            return 2;
        }
        tried += "\n\tno file '" + cand + "'";
    }
    lua_pushstring(L, tried.c_str());
    return 1;
}

static int printFn(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += '\t';
        out.append(s, len);
        lua_pop(L, 1);
    }
    log::write(LogLevel::Info, out);
    return 0;
}

static int tracebackHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (!msg) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

bool LuaVM::init() {
    L = luaL_newstate();
    if (!L) return false;
    *static_cast<LuaVM**>(lua_getextraspace(L)) = this;

    static const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},       {LUA_LOADLIBNAME, luaopen_package}, {LUA_COLIBNAME, luaopen_coroutine},
                                    {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},   {LUA_MATHLIBNAME, luaopen_math},
                                    {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_OSLIBNAME, luaopen_os}};
    for (const auto& lib : libs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }

    // Sandbox: games only see their own assets and the engine storage API.
    const char* removeGlobals[] = {"dofile", "loadfile"};
    for (const char* g : removeGlobals) { lua_pushnil(L); lua_setglobal(L, g); }
    lua_getglobal(L, "os");
    for (const char* f : {"execute", "remove", "rename", "tmpname", "exit", "getenv", "setlocale"}) { lua_pushnil(L); lua_setfield(L, -2, f); }
    lua_pop(L, 1);
    lua_getglobal(L, "package");
    lua_pushnil(L); lua_setfield(L, -2, "loadlib");
    lua_pushstring(L, ""); lua_setfield(L, -2, "cpath");
    lua_pushstring(L, ""); lua_setfield(L, -2, "path");
    // searchers = { preload, assets searcher }
    lua_newtable(L);
    lua_getfield(L, -2, "searchers");
    lua_rawgeti(L, -1, 1);
    lua_remove(L, -2);
    lua_rawseti(L, -2, 1);
    lua_pushcfunction(L, searcher);
    lua_rawseti(L, -2, 2);
    lua_setfield(L, -2, "searchers");
    lua_pop(L, 1);

    lua_pushcfunction(L, printFn);
    lua_setglobal(L, "print");

    lua_newtable(L);
    nodesTableRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    classesTableRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_newtable(L);
    lua_setglobal(L, "aether");
    bindCore(L);
    bindNode(L);
    bindSystems(L);

    if (luaL_loadbuffer(L, kPreludeSource, std::char_traits<char>::length(kPreludeSource), "=aether_prelude") != LUA_OK) {
        log::errorf("prelude failed to load: %s", lua_tostring(L, -1));
        return false;
    }
    if (!call(0, 0, "prelude")) return false;
    return true;
}

// ------------------------------------------------------------------ calling into Lua

bool LuaVM::call(lua_State* L, int nargs, int nresults, const char* what) {
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, tracebackHandler);
    lua_insert(L, base);
    int rc = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    if (rc == LUA_OK) return true;
    std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "(unknown error)";
    lua_pop(L, 1);
    errorCount++;
    static std::string lastMsg;
    static int suppressed = 0;
    if (msg == lastMsg) {
        suppressed++;
    } else {
        if (suppressed > 0) log::warnf("(previous error repeated %d more times)", suppressed);
        suppressed = 0;
        lastMsg = msg;
        log::errorf("script error in %s: %s", what, msg.c_str());
    }
    if (errorMessages.size() < 50) errorMessages.push_back(msg);
    return false;
}

void LuaVM::releaseRefs(const std::vector<int>& refs) {
    for (int r : refs) luaL_unref(L, LUA_REGISTRYINDEX, r);
}

void LuaVM::pushNode(lua_State* L, NodeId id) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, nodesTableRef);
    lua_rawgeti(L, -1, (lua_Integer)id);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        NodeId* ud = static_cast<NodeId*>(lua_newuserdatauv(L, sizeof(NodeId), 1));
        *ud = id;
        luaL_setmetatable(L, "aether.Node");
        lua_newtable(L);
        lua_setiuservalue(L, -2, 1);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, (lua_Integer)id);
    }
    lua_remove(L, -2);
}

NodeId LuaVM::toNode(lua_State* L, int idx) {
    void* p = luaL_testudata(L, idx, "aether.Node");
    return p ? *static_cast<NodeId*>(p) : kNoNode;
}

NodeId LuaVM::checkNode(lua_State* L, int idx) {
    void* p = luaL_testudata(L, idx, "aether.Node");
    if (!p) luaL_typeerror(L, idx, "node");
    return *static_cast<NodeId*>(p);
}

bool LuaVM::callNode(lua_State* L, NodeId id, const char* name, int nargs, const double* args) {
    Node* n = engine.tree.get(id);
    if (!n || !n->hasScript) return false;
    int top = lua_gettop(L);
    pushNode(L, id);
    lua_getiuservalue(L, -1, 1);
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
    lua_remove(L, -2);   // [ud, fn]
    lua_insert(L, -2);   // [fn, ud]
    for (int i = 0; i < nargs; i++) lua_pushnumber(L, args[i]);
    call(L, 1 + nargs, 0, name);
    lua_settop(L, top);
    return true;
}

bool LuaVM::callNodeNode(NodeId id, const char* name, NodeId other) {
    Node* n = engine.tree.get(id);
    if (!n || !n->hasScript) return false;
    int top = lua_gettop(L);
    pushNode(id);
    lua_getiuservalue(L, -1, 1);
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
    lua_remove(L, -2);
    lua_insert(L, -2);
    pushNode(other);
    call(2, 0, name);
    lua_settop(L, top);
    return true;
}

bool LuaVM::callNodeString(NodeId id, const char* name, const char* arg) {
    Node* n = engine.tree.get(id);
    if (!n || !n->hasScript) return false;
    int top = lua_gettop(L);
    pushNode(id);
    lua_getiuservalue(L, -1, 1);
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
    lua_remove(L, -2);
    lua_insert(L, -2);
    lua_pushstring(L, arg);
    call(2, 0, name);
    lua_settop(L, top);
    return true;
}

// ------------------------------------------------------------------ scripts and scenes

bool LuaVM::pushClass(lua_State* L, const std::string& path) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, classesTableRef);
    lua_getfield(L, -1, path.c_str());
    if (lua_istable(L, -1)) { lua_remove(L, -2); return true; }
    lua_pop(L, 1);

    std::string code;
    if (!engine.svc.assets || !engine.svc.assets->readText(path, code)) {
        log::errorf("script not found: '%s'", path.c_str());
        lua_pop(L, 1);
        return false;
    }
    if (luaL_loadbuffer(L, code.data(), code.size(), ("@" + path).c_str()) != LUA_OK) {
        log::errorf("syntax error: %s", lua_tostring(L, -1));
        errorCount++;
        lua_pop(L, 2);
        return false;
    }
    if (!call(L, 0, 1, path.c_str())) { lua_pop(L, 1); return false; }
    if (!lua_istable(L, -1)) {
        log::errorf("script '%s' must end with 'return <table>' (the script class)", path.c_str());
        errorCount++;
        lua_pop(L, 2);
        return false;
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, path.c_str());
    lua_remove(L, -2);
    return true;
}

void LuaVM::attachClass(lua_State* L, NodeId id, int classIdx, const std::string& path) {
    classIdx = lua_absindex(L, classIdx);
    Node* n = engine.tree.get(id);
    if (!n) return;
    pushNode(L, id);
    lua_getiuservalue(L, -1, 1);
    lua_newtable(L);
    lua_pushvalue(L, classIdx);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pop(L, 2);
    if (!n->hasScript) registerScriptNode(id);
    n->hasScript = true;
    n->scriptPath = path;
}

void LuaVM::registerScriptNode(NodeId id) { scriptNodes_.push_back(id); }

void LuaVM::clearRootScript() {
    NodeId root = engine.tree.root();
    if (Node* r = engine.tree.get(root)) { r->hasScript = false; r->scriptPath.clear(); }
    scriptNodes_.erase(std::remove(scriptNodes_.begin(), scriptNodes_.end(), root), scriptNodes_.end());
    lua_rawgeti(L, LUA_REGISTRYINDEX, nodesTableRef);
    lua_pushnil(L);
    lua_rawseti(L, -2, (lua_Integer)root);
    lua_pop(L, 1);
}

bool LuaVM::loadScene(const std::string& path) {
    clearRootScript();
    if (!pushClass(path)) return false;
    NodeId root = engine.tree.root();
    attachClass(root, -1, path);
    lua_pop(L, 1);
    engine.scenePath = path;
    callNode(root, "ready");
    return true;
}

bool LuaVM::runTestScript(const std::string& path) {
    std::string code;
    if (!engine.svc.assets || !engine.svc.assets->readText(path, code)) {
        log::errorf("test script not found: '%s'", path.c_str());
        return false;
    }
    if (luaL_loadbuffer(L, code.data(), code.size(), ("@" + path).c_str()) != LUA_OK) {
        log::errorf("syntax error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return call(0, 0, path.c_str());
}

// ------------------------------------------------------------------ per-frame callbacks

void LuaVM::update(float dt, const char* callback) {
    double d = dt;
    size_t n = scriptNodes_.size();
    for (size_t i = 0; i < n && i < scriptNodes_.size(); i++) {
        NodeId id = scriptNodes_[i];
        if (engine.tree.valid(id)) callNode(id, callback, 1, &d);
    }
}

void LuaVM::updateTweens(float dt) {
    int top = lua_gettop(L);
    lua_getglobal(L, "aether");
    lua_getfield(L, -1, "_update_tweens");
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, dt);
        call(1, 0, "tween");
    }
    lua_settop(L, top);
}

void LuaVM::fixedUpdate(float dt) {
    double d = dt;
    size_t n = scriptNodes_.size();
    for (size_t i = 0; i < n && i < scriptNodes_.size(); i++) {
        NodeId id = scriptNodes_[i];
        if (engine.tree.valid(id)) callNode(id, "fixed_update", 1, &d);
    }
}

void LuaVM::draw(bool ui) {
    size_t n = scriptNodes_.size();
    for (size_t i = 0; i < n && i < scriptNodes_.size(); i++) {
        NodeId id = scriptNodes_[i];
        if (engine.tree.valid(id)) callNode(id, ui ? "draw_ui" : "draw");
    }
}

void LuaVM::fireTimers(float dt) {
    engine.timers.update(dt, [&](const Timer& t) {
        if (t.owner != kNoNode && !engine.tree.valid(t.owner)) return false;
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, t.ref);
        bool keep = true;
        if (lua_isfunction(L, -1)) {
            if (call(0, 1, "timer")) keep = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
        }
        lua_settop(L, top);
        return keep;
    });
    releaseRefs(engine.timers.takeReleased());
}

void LuaVM::testTick(float dt) {
    int top = lua_gettop(L);
    lua_getglobal(L, "aether");
    lua_getfield(L, -1, "_test_tick");
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, dt);
        call(1, 0, "test");
    }
    lua_settop(L, top);
}

void LuaVM::collision(NodeId self, NodeId other, bool enter) {
    if (!engine.tree.valid(self) || !engine.tree.valid(other)) return;
    callNodeNode(self, enter ? "on_collision_enter" : "on_collision_exit", other);
}

void LuaVM::animationFinished(NodeId id, const std::string& name) { callNodeString(id, "on_animation_finished", name.c_str()); }

// ------------------------------------------------------------------ node lifecycle

void LuaVM::nodeDestroying(lua_State* L, NodeId id) {
    callNode(L, id, "destroyed");
    engine.timers.cancelOwner(id);
    if (engine.camera.follow == id) engine.camera.follow = kNoNode;
}

void LuaVM::destroyNode(lua_State* L, NodeId id) {
    if (id == engine.tree.root()) return;
    engine.tree.destroy(id, [&](NodeId n) { nodeDestroying(L, n); });
    engine.physics.invalidate();
}

void LuaVM::nodesFreed(const std::vector<NodeId>& ids) {
    if (ids.empty()) return;
    std::unordered_set<NodeId> dead(ids.begin(), ids.end());
    scriptNodes_.erase(std::remove_if(scriptNodes_.begin(), scriptNodes_.end(), [&](NodeId id) { return dead.count(id) > 0; }), scriptNodes_.end());
    lua_rawgeti(L, LUA_REGISTRYINDEX, nodesTableRef);
    for (NodeId id : ids) {
        lua_pushnil(L);
        lua_rawseti(L, -2, (lua_Integer)id);
    }
    lua_pop(L, 1);
}

}  // namespace aether
