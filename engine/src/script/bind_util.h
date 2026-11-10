// Internal helpers shared by the Lua binding files.
#pragma once
#include <string>

#include "lua_vm.h"

namespace aether {

constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;

// Colour: {r,g,b[,a]} (0..1, array or named), 0xRRGGBB / 0xRRGGBBAA number, "#rrggbb[aa]" or a colour name.
bool parseColor(lua_State* L, int idx, Color& out);
void pushColor(lua_State* L, const Color& c);

// Layer/mask: name, list of names, number, "all", "none".
bool parseLayerMask(lua_State* L, int idx, Physics& ph, uint32_t& out);

bool parseNodeType(const char* s, NodeType& out);

// Sets a native node property from the Lua value at valIdx. Returns false if `key` is not a native property.
bool nodeSetProp(lua_State* L, Node& n, const char* key, size_t len, int valIdx);
// Applies a table of properties to a node. Unknown keys become instance fields (accessible as self.<key>).
void nodeApplyProps(lua_State* L, NodeId id, int tblIdx);
// Creates a node of `type` as a child of `parent` (root if kNoNode) and applies props (may be 0 for none).
NodeId nodeCreate(lua_State* L, NodeType type, int propsIdx);
// Spawn: script path or class table at classIdx, optional props table at propsIdx (0 = none). Pushes the node.
int nodeSpawn(lua_State* L, int classIdx, int propsIdx);
// Declarative node tree: { type=, script=, name=, <props>..., children={ ... } }. Pushes the node.
int nodeBuild(lua_State* L, int specIdx);

// Registers fn (at fnIdx) as a timer; pushes nothing, returns the timer id.
int timerAdd(lua_State* L, NodeId owner, int fnIdx, float delay, float interval, int repeat);

inline float optNum(lua_State* L, int idx, float def) { return (float)luaL_optnumber(L, idx, def); }

}  // namespace aether
