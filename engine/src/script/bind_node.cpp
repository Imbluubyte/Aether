// Lua bindings for scene nodes: the "aether.Node" userdata (properties + methods), aether.node.*,
// aether.spawn and declarative node trees.
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include "aether/log.h"
#include "bind_util.h"

namespace aether {

// ------------------------------------------------------------------ colour / layer parsing

static bool hexNibble(char c, int& v) {
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;
    return true;
}

bool parseColor(lua_State* L, int idx, Color& out) {
    idx = lua_absindex(L, idx);
    int t = lua_type(L, idx);
    if (t == LUA_TNUMBER) {
        uint32_t v = (uint32_t)(lua_Integer)lua_tonumber(L, idx);
        out = lua_tonumber(L, idx) > 0xFFFFFF ? Color::fromRGBA8(v) : Color::fromRGBA8((v << 8) | 0xFF);
        return true;
    }
    if (t == LUA_TSTRING) {
        std::string s = lua_tostring(L, idx);
        if (!s.empty() && s[0] == '#' && (s.size() == 7 || s.size() == 9)) {
            uint32_t v = 0;
            for (size_t i = 1; i < s.size(); i++) {
                int nib;
                if (!hexNibble(s[i], nib)) return false;
                v = (v << 4) | (uint32_t)nib;
            }
            out = s.size() == 7 ? Color::fromRGBA8((v << 8) | 0xFF) : Color::fromRGBA8(v);
            return true;
        }
        static const struct { const char* n; uint32_t v; } names[] = {
            {"white", 0xffffffff}, {"black", 0x000000ff}, {"red", 0xe63946ff}, {"green", 0x52b788ff}, {"blue", 0x4895efff},
            {"yellow", 0xffd60aff}, {"orange", 0xff9f1cff}, {"purple", 0x9b5de5ff}, {"cyan", 0x4cc9f0ff}, {"magenta", 0xf15bb5ff},
            {"gray", 0x8d99aeff}, {"grey", 0x8d99aeff}, {"transparent", 0x00000000}, {"pink", 0xff8fabff}, {"brown", 0x8d6e63ff}};
        for (auto& e : names)
            if (s == e.n) { out = Color::fromRGBA8(e.v); return true; }
        return false;
    }
    if (t == LUA_TTABLE) {
        float c[4] = {1, 1, 1, 1};
        const char* keys[4] = {"r", "g", "b", "a"};
        bool any = false;
        for (int i = 0; i < 4; i++) {
            lua_getfield(L, idx, keys[i]);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_rawgeti(L, idx, i + 1); }
            if (lua_isnumber(L, -1)) { c[i] = (float)lua_tonumber(L, -1); any = true; }
            lua_pop(L, 1);
        }
        if (!any) return false;
        out = Color(c[0], c[1], c[2], c[3]);
        return true;
    }
    return false;
}

void pushColor(lua_State* L, const Color& c) {
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, c.r); lua_setfield(L, -2, "r");
    lua_pushnumber(L, c.g); lua_setfield(L, -2, "g");
    lua_pushnumber(L, c.b); lua_setfield(L, -2, "b");
    lua_pushnumber(L, c.a); lua_setfield(L, -2, "a");
}

bool parseLayerMask(lua_State* L, int idx, Physics& ph, uint32_t& out) {
    idx = lua_absindex(L, idx);
    int t = lua_type(L, idx);
    if (t == LUA_TNUMBER) { out = (uint32_t)(lua_Integer)lua_tonumber(L, idx); return true; }
    auto one = [&](const char* s, uint32_t& bit) {
        if (!strcmp(s, "all")) { bit = 0xffffffffu; return true; }
        if (!strcmp(s, "none")) { bit = 0; return true; }
        return ph.layers.bitFor(s, bit);
    };
    if (t == LUA_TSTRING) return one(lua_tostring(L, idx), out);
    if (t == LUA_TTABLE) {
        out = 0;
        int n = (int)luaL_len(L, idx);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, idx, i);
            uint32_t bit = 0;
            bool ok = false;
            if (lua_type(L, -1) == LUA_TSTRING) ok = one(lua_tostring(L, -1), bit);
            else if (lua_type(L, -1) == LUA_TNUMBER) { bit = (uint32_t)(lua_Integer)lua_tonumber(L, -1); ok = true; }
            lua_pop(L, 1);
            if (!ok) return false;
            out |= bit;
        }
        return true;
    }
    return false;
}

bool parseNodeType(const char* s, NodeType& out) {
    if (!strcmp(s, "node")) out = NodeType::Node;
    else if (!strcmp(s, "sprite")) out = NodeType::Sprite;
    else if (!strcmp(s, "anim") || !strcmp(s, "animated_sprite")) out = NodeType::AnimatedSprite;
    else if (!strcmp(s, "body")) out = NodeType::Body;
    else if (!strcmp(s, "label")) out = NodeType::Label;
    else return false;
    return true;
}

// ------------------------------------------------------------------ property table

namespace {

struct Prop {
    int (*get)(lua_State*, Node&);
    void (*set)(lua_State*, Node&, int);
};

float needNum(lua_State* L, int i, const char* prop) {
    if (!lua_isnumber(L, i)) luaL_error(L, "property '%s' expects a number, got %s", prop, luaL_typename(L, i));
    return (float)lua_tonumber(L, i);
}
const char* needStr(lua_State* L, int i, const char* prop) {
    if (lua_type(L, i) != LUA_TSTRING) luaL_error(L, "property '%s' expects a string, got %s", prop, luaL_typename(L, i));
    return lua_tostring(L, i);
}
void invalidatePhysics(lua_State* L) { vmOf(L).engine.physics.invalidate(); }

#define GET_NUM(expr) [](lua_State* L, Node& n) { (void)n; lua_pushnumber(L, (expr)); return 1; }
#define GET_BOOL(expr) [](lua_State* L, Node& n) { (void)n; lua_pushboolean(L, (expr)); return 1; }
#define SET_NUM(name, stmt) [](lua_State* L, Node& n, int i) { float v = needNum(L, i, name); (void)v; stmt; }

int pushLayerValue(lua_State* L, uint32_t bits) {
    std::string name = vmOf(L).engine.physics.layers.nameFor(bits);
    if (!name.empty()) lua_pushstring(L, name.c_str());
    else lua_pushinteger(L, (lua_Integer)bits);
    return 1;
}

const std::unordered_map<std::string_view, Prop>& props() {
    static const std::unordered_map<std::string_view, Prop> table = {
        {"name", {[](lua_State* L, Node& n) { lua_pushstring(L, n.name.c_str()); return 1; },
                  [](lua_State* L, Node& n, int i) { n.name = needStr(L, i, "name"); }}},
        {"x", {GET_NUM(n.pos.x), [](lua_State* L, Node& n, int i) { n.pos.x = needNum(L, i, "x"); if (n.type == NodeType::Body && n.body == BodyType::Static) invalidatePhysics(L); }}},
        {"y", {GET_NUM(n.pos.y), [](lua_State* L, Node& n, int i) { n.pos.y = needNum(L, i, "y"); if (n.type == NodeType::Body && n.body == BodyType::Static) invalidatePhysics(L); }}},
        {"world_x", {GET_NUM(n.worldPos.x), nullptr}},
        {"world_y", {GET_NUM(n.worldPos.y), nullptr}},
        {"rotation", {GET_NUM(n.rot * kRadToDeg), SET_NUM("rotation", n.rot = v * kDegToRad)}},
        {"scale_x", {GET_NUM(n.scale.x), SET_NUM("scale_x", n.scale.x = v)}},
        {"scale_y", {GET_NUM(n.scale.y), SET_NUM("scale_y", n.scale.y = v)}},
        {"scale", {GET_NUM(n.scale.x), SET_NUM("scale", n.scale = Vec2(v, v))}},
        {"visible", {GET_BOOL(n.visible), [](lua_State* L, Node& n, int i) { n.visible = lua_toboolean(L, i) != 0; }}},
        {"screen_space", {GET_BOOL(n.screenSpace), [](lua_State* L, Node& n, int i) { n.screenSpace = lua_toboolean(L, i) != 0; }}},
        {"z", {GET_NUM(n.z), SET_NUM("z", n.z = (int)v)}},
        {"w", {GET_NUM(n.w), [](lua_State* L, Node& n, int i) { n.w = needNum(L, i, "w"); invalidatePhysics(L); }}},
        {"h", {GET_NUM(n.h), [](lua_State* L, Node& n, int i) { n.h = needNum(L, i, "h"); invalidatePhysics(L); }}},
        {"color", {[](lua_State* L, Node& n) { pushColor(L, n.color); return 1; },
                   [](lua_State* L, Node& n, int i) {
                       Color c;
                       if (!parseColor(L, i, c)) luaL_error(L, "property 'color' expects {r,g,b,a}, 0xRRGGBB, '#rrggbb' or a colour name");
                       n.color = c;
                       n.hasColor = true;
                   }}},
        {"opacity", {GET_NUM(n.color.a), [](lua_State* L, Node& n, int i) { n.color.a = clampf(needNum(L, i, "opacity"), 0, 1); n.hasColor = true; }}},
        {"texture", {[](lua_State* L, Node& n) {
                         if (n.texturePath.empty()) lua_pushnil(L); else lua_pushstring(L, n.texturePath.c_str());
                         return 1;
                     },
                     [](lua_State* L, Node& n, int i) {
                         if (lua_isnil(L, i)) { n.texturePath.clear(); n.texture = 0; n.texW = n.texH = 0; return; }
                         n.texturePath = needStr(L, i, "texture");
                         const TextureInfo& t = vmOf(L).engine.resources.texture(n.texturePath);
                         n.texture = t.id; n.texW = t.w; n.texH = t.h;
                         invalidatePhysics(L);
                     }}},
        {"region", {[](lua_State* L, Node& n) {
                        lua_createtable(L, 0, 4);
                        lua_pushnumber(L, n.region.x); lua_setfield(L, -2, "x");
                        lua_pushnumber(L, n.region.y); lua_setfield(L, -2, "y");
                        lua_pushnumber(L, n.region.w); lua_setfield(L, -2, "w");
                        lua_pushnumber(L, n.region.h); lua_setfield(L, -2, "h");
                        return 1;
                    },
                    [](lua_State* L, Node& n, int i) {
                        if (lua_isnil(L, i)) { n.region = Rect{}; return; }
                        if (!lua_istable(L, i)) luaL_error(L, "property 'region' expects {x,y,w,h} in texture pixels");
                        float v[4] = {0, 0, 0, 0};
                        const char* keys[4] = {"x", "y", "w", "h"};
                        for (int k = 0; k < 4; k++) {
                            lua_getfield(L, i, keys[k]);
                            if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_rawgeti(L, i, k + 1); }
                            v[k] = (float)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                        }
                        n.region = Rect{v[0], v[1], v[2], v[3]};
                    }}},
        {"flip_x", {GET_BOOL(n.flipX), [](lua_State* L, Node& n, int i) { n.flipX = lua_toboolean(L, i) != 0; }}},
        {"flip_y", {GET_BOOL(n.flipY), [](lua_State* L, Node& n, int i) { n.flipY = lua_toboolean(L, i) != 0; }}},
        {"origin_x", {GET_NUM(n.origin.x), SET_NUM("origin_x", n.origin.x = v)}},
        {"origin_y", {GET_NUM(n.origin.y), SET_NUM("origin_y", n.origin.y = v)}},
        {"frame_w", {GET_NUM(n.frameW), SET_NUM("frame_w", n.frameW = (int)v)}},
        {"frame_h", {GET_NUM(n.frameH), SET_NUM("frame_h", n.frameH = (int)v)}},
        {"frame", {GET_NUM(n.cell + 1), SET_NUM("frame", n.cell = (int)v - 1; n.animPlaying = false)}},
        {"anim_speed", {GET_NUM(n.animSpeed), SET_NUM("anim_speed", n.animSpeed = v)}},
        {"playing", {GET_BOOL(n.animPlaying), nullptr}},
        {"anim", {[](lua_State* L, Node& n) {
                      if (n.animIndex < 0) lua_pushnil(L); else lua_pushstring(L, n.animations[(size_t)n.animIndex].name.c_str());
                      return 1;
                  },
                  [](lua_State* L, Node& n, int i) {
                      const char* name = needStr(L, i, "anim");
                      for (size_t k = 0; k < n.animations.size(); k++)
                          if (n.animations[k].name == name) {
                              if ((int)k != n.animIndex || !n.animPlaying) { n.animIndex = (int)k; n.animFrame = 0; n.animTime = 0; n.animPlaying = true; if (!n.animations[k].frames.empty()) n.cell = n.animations[k].frames[0]; }
                              return;
                          }
                      luaL_error(L, "no animation named '%s' (define it with node.animations = {...})", name);
                  }}},
        {"animations", {nullptr, [](lua_State* L, Node& n, int i) {
                            if (!lua_istable(L, i)) luaL_error(L, "property 'animations' expects a table: { walk = {frames={1,2,3}, fps=8} }");
                            n.animations.clear();
                            n.animIndex = -1;
                            n.animPlaying = false;
                            lua_pushnil(L);
                            while (lua_next(L, i)) {
                                if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
                                    Animation a;
                                    a.name = lua_tostring(L, -2);
                                    int d = lua_gettop(L);
                                    lua_getfield(L, d, "fps");
                                    if (lua_isnumber(L, -1)) a.fps = (float)lua_tonumber(L, -1);
                                    lua_pop(L, 1);
                                    lua_getfield(L, d, "loop");
                                    if (!lua_isnil(L, -1)) a.loop = lua_toboolean(L, -1) != 0;
                                    lua_pop(L, 1);
                                    lua_getfield(L, d, "frames");
                                    if (lua_istable(L, -1)) {
                                        int cnt = (int)luaL_len(L, -1);
                                        for (int k = 1; k <= cnt; k++) {
                                            lua_rawgeti(L, -1, k);
                                            a.frames.push_back((int)lua_tointeger(L, -1) - 1);
                                            lua_pop(L, 1);
                                        }
                                    }
                                    lua_pop(L, 1);
                                    if (a.frames.empty()) {
                                        lua_getfield(L, d, "from");
                                        lua_getfield(L, d, "to");
                                        if (lua_isnumber(L, -2) && lua_isnumber(L, -1))
                                            for (int f = (int)lua_tointeger(L, -2); f <= (int)lua_tointeger(L, -1); f++) a.frames.push_back(f - 1);
                                        lua_pop(L, 2);
                                    }
                                    if (a.frames.empty()) luaL_error(L, "animation '%s' needs frames={...} or from/to", a.name.c_str());
                                    n.animations.push_back(std::move(a));
                                }
                                lua_pop(L, 1);
                            }
                        }}},
        {"text", {[](lua_State* L, Node& n) { lua_pushstring(L, n.text.c_str()); return 1; },
                  [](lua_State* L, Node& n, int i) {
                      size_t len;
                      const char* s = luaL_tolstring(L, i, &len);
                      n.text.assign(s, len);
                      lua_pop(L, 1);
                  }}},
        {"text_size", {GET_NUM(n.textSize), SET_NUM("text_size", n.textSize = v)}},
        {"align", {[](lua_State* L, Node& n) { lua_pushstring(L, n.align == 1 ? "center" : (n.align == 2 ? "right" : "left")); return 1; },
                   [](lua_State* L, Node& n, int i) {
                       std::string s = needStr(L, i, "align");
                       n.align = s == "center" ? 1 : (s == "right" ? 2 : 0);
                   }}},
        {"body_type", {[](lua_State* L, Node& n) {
                           const char* names[] = {"static", "dynamic", "kinematic", "trigger"};
                           lua_pushstring(L, names[(int)n.body]);
                           return 1;
                       },
                       [](lua_State* L, Node& n, int i) {
                           std::string s = needStr(L, i, "body_type");
                           if (s == "static") n.body = BodyType::Static;
                           else if (s == "dynamic") n.body = BodyType::Dynamic;
                           else if (s == "kinematic") n.body = BodyType::Kinematic;
                           else if (s == "trigger") n.body = BodyType::Trigger;
                           else luaL_error(L, "body_type must be 'static', 'dynamic', 'kinematic' or 'trigger' (got '%s')", s.c_str());
                           invalidatePhysics(L);
                       }}},
        {"shape", {[](lua_State* L, Node& n) { lua_pushstring(L, n.circle ? "circle" : "box"); return 1; },
                   [](lua_State* L, Node& n, int i) {
                       std::string s = needStr(L, i, "shape");
                       if (s != "box" && s != "circle") luaL_error(L, "shape must be 'box' or 'circle'");
                       n.circle = s == "circle";
                       invalidatePhysics(L);
                   }}},
        {"radius", {GET_NUM(n.radius), [](lua_State* L, Node& n, int i) { n.radius = needNum(L, i, "radius"); n.circle = n.radius > 0; invalidatePhysics(L); }}},
        {"layer", {[](lua_State* L, Node& n) { return pushLayerValue(L, n.layer); },
                   [](lua_State* L, Node& n, int i) {
                       uint32_t v;
                       if (!parseLayerMask(L, i, vmOf(L).engine.physics, v)) luaL_error(L, "unknown layer (declare names with aether.physics.set_layers)");
                       n.layer = v;
                       invalidatePhysics(L);
                   }}},
        {"mask", {GET_NUM(n.mask), [](lua_State* L, Node& n, int i) {
                      uint32_t v;
                      if (!parseLayerMask(L, i, vmOf(L).engine.physics, v)) luaL_error(L, "unknown layer in mask (declare names with aether.physics.set_layers)");
                      n.mask = v;
                      invalidatePhysics(L);
                  }}},
        {"vx", {GET_NUM(n.vel.x), SET_NUM("vx", n.vel.x = v)}},
        {"vy", {GET_NUM(n.vel.y), SET_NUM("vy", n.vel.y = v)}},
        {"gravity_scale", {GET_NUM(n.gravityScale), SET_NUM("gravity_scale", n.gravityScale = v)}},
        {"on_floor", {GET_BOOL(n.onFloor), nullptr}},
        {"on_ceiling", {GET_BOOL(n.onCeiling), nullptr}},
        {"on_wall", {GET_BOOL(n.onWall), nullptr}},
        {"active", {GET_BOOL(n.bodyActive), [](lua_State* L, Node& n, int i) { n.bodyActive = lua_toboolean(L, i) != 0; invalidatePhysics(L); }}},
        {"type", {[](lua_State* L, Node& n) {
                      const char* names[] = {"node", "sprite", "anim", "body", "label"};
                      lua_pushstring(L, names[(int)n.type]);
                      return 1;
                  },
                  nullptr}},
        {"alive", {GET_BOOL(!n.destroyed), nullptr}},
        {"script", {[](lua_State* L, Node& n) { if (n.hasScript) lua_pushstring(L, n.scriptPath.c_str()); else lua_pushnil(L); return 1; }, nullptr}},
        {"parent", {[](lua_State* L, Node& n) {
                        if (n.parent == kNoNode) lua_pushnil(L); else vmOf(L).pushNode(L, n.parent);
                        return 1;
                    },
                    [](lua_State* L, Node& n, int i) {
                        LuaVM& vm = vmOf(L);
                        NodeId p = lua_isnil(L, i) ? vm.engine.tree.root() : vm.toNode(L, i);
                        if (p == kNoNode) luaL_error(L, "property 'parent' expects a node or nil");
                        if (!vm.engine.tree.addChild(p, n.id)) luaL_error(L, "cannot parent a node to itself or to its own descendant");
                    }}},
    };
    return table;
}

Node& liveNode(lua_State* L, NodeId id, const char* what) {
    Node* n = vmOf(L).engine.tree.get(id);
    if (!n || n->destroyed) luaL_error(L, "cannot use '%s': the node was destroyed (check node:is_valid() before using stored references)", what);
    return *n;
}

}  // namespace

bool nodeSetProp(lua_State* L, Node& n, const char* key, size_t len, int valIdx) {
    auto& t = props();
    auto it = t.find(std::string_view(key, len));
    if (it == t.end()) return false;
    if (!it->second.set) luaL_error(L, "property '%s' is read-only", key);
    it->second.set(L, n, lua_absindex(L, valIdx));
    std::string_view k(key, len);
    if (k == "x" || k == "y" || k == "rotation" || k == "scale" || k == "scale_x" || k == "scale_y" || k == "parent")
        vmOf(L).engine.tree.updateNodeTransform(n);
    return true;
}

// ------------------------------------------------------------------ the node metatable

static int nodeIndexFn(lua_State* L) {
    NodeId id = *static_cast<NodeId*>(lua_touserdata(L, 1));
    if (lua_type(L, 2) == LUA_TSTRING) {
        size_t len;
        const char* key = lua_tolstring(L, 2, &len);
        if (len == 5 && !strcmp(key, "alive")) { lua_pushboolean(L, vmOf(L).engine.tree.valid(id)); return 1; }
        auto& t = props();
        auto it = t.find(std::string_view(key, len));
        if (it != t.end() && it->second.get) {
            Node& n = liveNode(L, id, key);
            return it->second.get(L, n);
        }
        lua_pushvalue(L, 2);
        lua_gettable(L, lua_upvalueindex(1));  // native methods
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 1);
    }
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}

static int nodeNewIndexFn(lua_State* L) {
    NodeId id = *static_cast<NodeId*>(lua_touserdata(L, 1));
    if (lua_type(L, 2) == LUA_TSTRING) {
        size_t len;
        const char* key = lua_tolstring(L, 2, &len);
        auto& t = props();
        auto it = t.find(std::string_view(key, len));
        if (it != t.end()) {
            if (!it->second.set) luaL_error(L, "property '%s' is read-only", key);
            Node& n = liveNode(L, id, key);
            nodeSetProp(L, n, key, len, 3);
            return 0;
        }
    }
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
    return 0;
}

static int nodeToString(lua_State* L) {
    NodeId id = *static_cast<NodeId*>(lua_touserdata(L, 1));
    Node* n = vmOf(L).engine.tree.get(id);
    if (!n) lua_pushfstring(L, "Node<destroyed #%d>", (int)nodeIndex(id));
    else {
        const char* names[] = {"node", "sprite", "anim", "body", "label"};
        lua_pushfstring(L, "Node<%s '%s' #%d>", names[(int)n->type], n->name.c_str(), (int)nodeIndex(id));
    }
    return 1;
}

// ------------------------------------------------------------------ props application / creation

void nodeApplyProps(lua_State* L, NodeId id, int tblIdx) {
    tblIdx = lua_absindex(L, tblIdx);
    LuaVM& vm = vmOf(L);
    lua_pushnil(L);
    while (lua_next(L, tblIdx)) {
        if (lua_type(L, -2) != LUA_TSTRING) { lua_pop(L, 1); continue; }
        size_t len;
        const char* key = lua_tolstring(L, -2, &len);
        std::string_view k(key, len);
        if (k == "type" || k == "script" || k == "children") { lua_pop(L, 1); continue; }
        Node* n = vm.engine.tree.get(id);
        if (!n) { lua_pop(L, 1); break; }
        if (k == "groups" && lua_istable(L, -1)) {
            int cnt = (int)luaL_len(L, -1);
            for (int i = 1; i <= cnt; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_type(L, -1) == LUA_TSTRING) vm.engine.tree.addToGroup(id, lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else if (!nodeSetProp(L, *n, key, len, -1)) {
            // Unknown key: becomes an instance field (self.<key>).
            vm.pushNode(L, id);
            lua_getiuservalue(L, -1, 1);
            lua_pushvalue(L, -4);
            lua_pushvalue(L, -4);
            lua_rawset(L, -3);
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }
    if (Node* n = vm.engine.tree.get(id)) vm.engine.tree.updateNodeTransform(*n);
}

NodeId nodeCreate(lua_State* L, NodeType type, int propsIdx) {
    LuaVM& vm = vmOf(L);
    NodeId id = vm.engine.tree.create(type);
    if (type == NodeType::Body) vm.engine.physics.invalidate();
    if (propsIdx != 0 && lua_istable(L, propsIdx)) nodeApplyProps(L, id, propsIdx);
    return id;
}

static bool pushClassArg(lua_State* L, int idx, std::string& pathOut) {
    idx = lua_absindex(L, idx);
    if (lua_type(L, idx) == LUA_TSTRING) {
        pathOut = lua_tostring(L, idx);
        return vmOf(L).pushClass(L, pathOut);
    }
    if (lua_istable(L, idx)) {
        pathOut = "(inline)";
        lua_pushvalue(L, idx);
        return true;
    }
    luaL_error(L, "expected a script path or a script table");
    return false;
}

static NodeType classNodeType(lua_State* L, int classIdx, NodeType def) {
    lua_getfield(L, classIdx, "type");
    NodeType t = def;
    if (lua_type(L, -1) == LUA_TSTRING && !parseNodeType(lua_tostring(L, -1), t))
        luaL_error(L, "script has unknown node type '%s' (use node, sprite, anim, body or label)", lua_tostring(L, -1));
    lua_pop(L, 1);
    return t;
}

int nodeSpawn(lua_State* L, int classArg, int propsIdx) {
    LuaVM& vm = vmOf(L);
    std::string path;
    if (!pushClassArg(L, classArg, path)) return luaL_error(L, "spawn failed: could not load script '%s' (see log)", lua_tostring(L, classArg));
    int cls = lua_gettop(L);
    NodeType type = classNodeType(L, cls, NodeType::Node);
    NodeId id = vm.engine.tree.create(type);
    if (type == NodeType::Body) vm.engine.physics.invalidate();
    lua_getfield(L, cls, "props");
    if (lua_istable(L, -1)) nodeApplyProps(L, id, lua_gettop(L));
    lua_pop(L, 1);
    vm.attachClass(L, id, cls, path);
    if (propsIdx != 0 && lua_istable(L, propsIdx)) nodeApplyProps(L, id, propsIdx);
    vm.callNode(L, id, "ready");
    lua_settop(L, cls - 1);
    vm.pushNode(L, id);
    return 1;
}

int nodeBuild(lua_State* L, int specIdx) {
    specIdx = lua_absindex(L, specIdx);
    LuaVM& vm = vmOf(L);
    luaL_checktype(L, specIdx, LUA_TTABLE);
    NodeType type = NodeType::Node;
    lua_getfield(L, specIdx, "type");
    if (lua_type(L, -1) == LUA_TSTRING && !parseNodeType(lua_tostring(L, -1), type))
        luaL_error(L, "unknown node type '%s'", lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, specIdx, "script");
    bool hasScript = !lua_isnil(L, -1);
    int cls = 0;
    std::string path;
    if (hasScript) {
        if (!pushClassArg(L, -1, path)) luaL_error(L, "could not load script '%s'", lua_tostring(L, -2));
        cls = lua_gettop(L);
        if (type == NodeType::Node) type = classNodeType(L, cls, NodeType::Node);
    }
    NodeId id = vm.engine.tree.create(type);
    if (type == NodeType::Body) vm.engine.physics.invalidate();
    if (hasScript) {
        lua_getfield(L, cls, "props");
        if (lua_istable(L, -1)) nodeApplyProps(L, id, lua_gettop(L));
        lua_pop(L, 1);
        vm.attachClass(L, id, cls, path);
    }
    nodeApplyProps(L, id, specIdx);

    lua_getfield(L, specIdx, "children");
    if (lua_istable(L, -1)) {
        int cnt = (int)luaL_len(L, -1);
        for (int i = 1; i <= cnt; i++) {
            lua_rawgeti(L, -1, i);
            nodeBuild(L, -1);  // pushes child
            NodeId child = vm.toNode(L, -1);
            vm.engine.tree.addChild(id, child);
            lua_pop(L, 2);
        }
    }
    lua_pop(L, 1);
    if (hasScript) vm.callNode(L, id, "ready");
    lua_settop(L, specIdx);
    vm.pushNode(L, id);
    return 1;
}

// ------------------------------------------------------------------ methods

static int timerAddImpl(lua_State* L, NodeId owner, int fnIdx, float delay, float interval, int repeat) {
    luaL_checktype(L, fnIdx, LUA_TFUNCTION);
    lua_pushvalue(L, fnIdx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return vmOf(L).engine.timers.add(delay, interval, repeat, ref, owner);
}

int timerAdd(lua_State* L, NodeId owner, int fnIdx, float delay, float interval, int repeat) {
    return timerAddImpl(L, owner, lua_absindex(L, fnIdx), delay, interval, repeat);
}

static Node& selfNode(lua_State* L, NodeId& id, const char* method) {
    id = vmOf(L).checkNode(L, 1);
    return liveNode(L, id, method);
}

static int m_destroy(lua_State* L) {
    NodeId id = vmOf(L).checkNode(L, 1);
    vmOf(L).destroyNode(L, id);
    return 0;
}
static int m_isValid(lua_State* L) {
    NodeId id = vmOf(L).checkNode(L, 1);
    lua_pushboolean(L, vmOf(L).engine.tree.valid(id));
    return 1;
}
static int m_addChild(lua_State* L) {
    NodeId id;
    selfNode(L, id, "add_child");
    NodeId c = vmOf(L).checkNode(L, 2);
    if (!vmOf(L).engine.tree.addChild(id, c)) return luaL_error(L, "add_child failed (invalid node or it would create a cycle)");
    return 0;
}
static int m_getChild(lua_State* L) {
    NodeId id;
    selfNode(L, id, "get_child");
    NodeId c = vmOf(L).engine.tree.findChild(id, luaL_checkstring(L, 2), false);
    if (c == kNoNode) lua_pushnil(L); else vmOf(L).pushNode(L, c);
    return 1;
}
static int m_find(lua_State* L) {
    NodeId id;
    selfNode(L, id, "find");
    NodeId c = vmOf(L).engine.tree.findChild(id, luaL_checkstring(L, 2), true);
    if (c == kNoNode) lua_pushnil(L); else vmOf(L).pushNode(L, c);
    return 1;
}
static int m_children(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "children");
    lua_createtable(L, (int)n.children.size(), 0);
    int i = 1;
    for (NodeId c : n.children)
        if (vmOf(L).engine.tree.valid(c)) { vmOf(L).pushNode(L, c); lua_rawseti(L, -2, i++); }
    return 1;
}
static int m_addToGroup(lua_State* L) {
    NodeId id;
    selfNode(L, id, "add_to_group");
    vmOf(L).engine.tree.addToGroup(id, luaL_checkstring(L, 2));
    return 0;
}
static int m_removeFromGroup(lua_State* L) {
    NodeId id;
    selfNode(L, id, "remove_from_group");
    vmOf(L).engine.tree.removeFromGroup(id, luaL_checkstring(L, 2));
    return 0;
}
static int m_inGroup(lua_State* L) {
    NodeId id;
    selfNode(L, id, "in_group");
    lua_pushboolean(L, vmOf(L).engine.tree.inGroup(id, luaL_checkstring(L, 2)));
    return 1;
}
static int m_groups(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "groups");
    lua_createtable(L, (int)n.groups.size(), 0);
    for (size_t i = 0; i < n.groups.size(); i++) { lua_pushstring(L, n.groups[i].c_str()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}
static int m_after(lua_State* L) {
    NodeId id;
    selfNode(L, id, "after");
    float sec = (float)luaL_checknumber(L, 2);
    lua_pushinteger(L, timerAddImpl(L, id, 3, sec, 0, 0));
    return 1;
}
static int m_every(lua_State* L) {
    NodeId id;
    selfNode(L, id, "every");
    float sec = (float)luaL_checknumber(L, 2);
    if (sec <= 0) return luaL_error(L, "every() needs an interval greater than 0");
    int count = (int)luaL_optinteger(L, 4, -1);
    lua_pushinteger(L, timerAddImpl(L, id, 3, sec, sec, count));
    return 1;
}
static int m_setScript(lua_State* L) {
    NodeId id;
    selfNode(L, id, "set_script");
    LuaVM& vm = vmOf(L);
    std::string path;
    if (!pushClassArg(L, 2, path)) return luaL_error(L, "set_script: could not load script (see log)");
    int cls = lua_gettop(L);
    vm.attachClass(L, id, cls, path);
    if (lua_istable(L, 3)) nodeApplyProps(L, id, 3);
    vm.callNode(L, id, "ready");
    lua_settop(L, 1);
    return 1;
}
static int m_set(lua_State* L) {
    NodeId id;
    selfNode(L, id, "set");
    luaL_checktype(L, 2, LUA_TTABLE);
    nodeApplyProps(L, id, 2);
    lua_settop(L, 1);
    return 1;
}
static int m_play(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "play");
    const char* name = luaL_checkstring(L, 2);
    bool restart = lua_toboolean(L, 3) != 0;
    for (size_t k = 0; k < n.animations.size(); k++) {
        if (n.animations[k].name != name) continue;
        if ((int)k == n.animIndex && n.animPlaying && !restart) return 0;
        n.animIndex = (int)k; n.animFrame = 0; n.animTime = 0; n.animPlaying = true;
        n.cell = n.animations[k].frames[0];
        return 0;
    }
    std::string known;
    for (auto& a : n.animations) known += (known.empty() ? "" : ", ") + a.name;
    return luaL_error(L, "no animation named '%s' (defined: %s)", name, known.empty() ? "none - set node.animations first" : known.c_str());
}
static int m_stopAnim(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "stop_anim");
    n.animPlaying = false;
    return 0;
}
static int m_move(lua_State* L) {
    NodeId id;
    selfNode(L, id, "move");
    bool hx, hy;
    vmOf(L).engine.physics.move(id, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), hx, hy);
    lua_pushboolean(L, hx);
    lua_pushboolean(L, hy);
    return 2;
}

// Resolve a target given as a node or as (x, y) starting at argument 2.
static Vec2 targetArg(lua_State* L, int idx) {
    NodeId t = vmOf(L).toNode(L, idx);
    if (t != kNoNode) return liveNode(L, t, "target").worldPos;
    return {(float)luaL_checknumber(L, idx), (float)luaL_checknumber(L, idx + 1)};
}
static int m_distanceTo(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "distance_to");
    lua_pushnumber(L, (targetArg(L, 2) - n.worldPos).length());
    return 1;
}
static int m_angleTo(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "angle_to");
    Vec2 d = targetArg(L, 2) - n.worldPos;
    lua_pushnumber(L, std::atan2(d.y, d.x) * kRadToDeg);
    return 1;
}
static int m_lookAt(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "look_at");
    Vec2 d = targetArg(L, 2) - n.worldPos;
    n.rot = std::atan2(d.y, d.x);
    return 0;
}
static int m_directionTo(lua_State* L) {
    NodeId id;
    Node& n = selfNode(L, id, "direction_to");
    Vec2 d = targetArg(L, 2) - n.worldPos;
    float len = d.length();
    if (len > 1e-6f) d = d * (1.f / len); else d = {0, 0};
    lua_pushnumber(L, d.x);
    lua_pushnumber(L, d.y);
    return 2;
}

void bindNode(lua_State* L) {
    static const luaL_Reg methods[] = {
        {"destroy", m_destroy}, {"is_valid", m_isValid}, {"add_child", m_addChild}, {"get_child", m_getChild}, {"find", m_find},
        {"children", m_children}, {"add_to_group", m_addToGroup}, {"remove_from_group", m_removeFromGroup}, {"in_group", m_inGroup},
        {"groups", m_groups}, {"after", m_after}, {"every", m_every}, {"set_script", m_setScript}, {"set", m_set}, {"play", m_play},
        {"stop_anim", m_stopAnim}, {"move", m_move}, {"distance_to", m_distanceTo}, {"angle_to", m_angleTo}, {"look_at", m_lookAt},
        {"direction_to", m_directionTo}, {nullptr, nullptr}};
    luaL_newmetatable(L, "aether.Node");
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_pushcclosure(L, nodeIndexFn, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, nodeNewIndexFn);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, nodeToString);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
}

}  // namespace aether
