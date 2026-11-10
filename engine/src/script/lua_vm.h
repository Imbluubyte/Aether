// Internal: the embedded Lua runtime and the glue between engine nodes and Lua objects.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Lua is compiled as C++ (see engine/CMakeLists.txt): luaL_error then throws a C++ exception instead
// of longjmp-ing, so destructors of std::string/vector locals in binding code always run.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include "aether/engine.h"

namespace aether {

class LuaVM {
public:
    explicit LuaVM(Engine& e);
    ~LuaVM();

    bool init();
    lua_State* L = nullptr;
    Engine& engine;

    // --- scene / script lifecycle ---
    // Attach the script at `path` to the scene root (discarding the previous scene script) and call ready.
    bool loadScene(const std::string& path);
    void clearRootScript();
    bool runTestScript(const std::string& path);

    // Pushes the class table for a script path (cached). Returns false (nothing pushed) on error.
    bool pushClass(lua_State* L, const std::string& path);
    bool pushClass(const std::string& path) { return pushClass(L, path); }
    // Attaches the class table at stack index `classIdx` to the node (does not call ready).
    void attachClass(lua_State* L, NodeId id, int classIdx, const std::string& path);
    void attachClass(NodeId id, int classIdx, const std::string& path) { attachClass(L, id, classIdx, path); }
    // Calls node:<name>(...) if the node has such a method. Returns true if it was called.
    // Every helper that touches the Lua stack has an overload taking the *calling* lua_State: bindings
    // may run inside a coroutine, whose stack differs from the main thread's.
    bool callNode(lua_State* L, NodeId id, const char* name, int nargs = 0, const double* args = nullptr);
    bool callNode(NodeId id, const char* name, int nargs = 0, const double* args = nullptr) { return callNode(L, id, name, nargs, args); }
    bool callNodeNode(NodeId id, const char* name, NodeId other);
    bool callNodeString(NodeId id, const char* name, const char* arg);

    // --- per frame ---
    void update(float dt, const char* callback = "update");
    void updateTweens(float dt);
    void fixedUpdate(float dt);
    void draw(bool ui);
    void fireTimers(float dt);
    void testTick(float dt);
    void collision(NodeId self, NodeId other, bool enter);
    void animationFinished(NodeId id, const std::string& name);

    // --- node lifecycle ---
    void nodeDestroying(lua_State* L, NodeId id);   // before it is flagged destroyed
    void nodesFreed(const std::vector<NodeId>& ids);
    void registerScriptNode(NodeId id);
    void destroyNode(lua_State* L, NodeId id);      // destroy subtree with script callbacks + timers
    void destroyNode(NodeId id) { destroyNode(L, id); }

    // --- helpers for bindings ---
    void pushNode(lua_State* L, NodeId id);         // pushes the (cached) Lua handle for a node
    void pushNode(NodeId id) { pushNode(L, id); }
    NodeId checkNode(lua_State* L, int idx);        // luaL_error if not a node
    NodeId toNode(lua_State* L, int idx);           // kNoNode if not a node
    bool call(lua_State* L, int nargs, int nresults, const char* what);  // pcall with traceback + logging
    bool call(int nargs, int nresults, const char* what) { return call(L, nargs, nresults, what); }
    void releaseRefs(const std::vector<int>& refs);

    int errorCount = 0;
    int errorsThisFrame = 0;
    std::vector<std::string> errorMessages;
    int nodesTableRef = LUA_NOREF;
    int classesTableRef = LUA_NOREF;

private:
    void registerAll();
    std::vector<NodeId> scriptNodes_;
};

LuaVM& vmOf(lua_State* L);

// Binding registration (one per file).
void bindCore(lua_State* L);
void bindNode(lua_State* L);
void bindSystems(lua_State* L);
extern const char* kPreludeSource;

}  // namespace aether
