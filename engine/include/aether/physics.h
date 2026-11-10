// 2D physics: box/circle colliders, layers+masks, trigger/overlap events, solid static geometry for
// dynamic bodies (platformers), circle/rect queries and raycasts. Deliberately simple and fast
// enough for bullet-heaven style games with thousands of overlapping bodies.
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "aether/scene.h"

namespace aether {

struct RayHit {
    bool hit = false;
    NodeId node = kNoNode;
    Vec2 point;
    Vec2 normal;
    float distance = 0;
};

class Layers {
public:
    // Names are assigned bits 0..31 in order. Names persist for the lifetime of the engine session.
    void set(const std::vector<std::string>& names);
    bool bitFor(const std::string& name, uint32_t& bit) const;
    std::string nameFor(uint32_t bit) const;
    const std::vector<std::string>& names() const { return names_; }

private:
    std::vector<std::string> names_;
};

class Physics {
public:
    using Dispatch = std::function<void(NodeId self, NodeId other, bool enter)>;

    explicit Physics(SceneTree& tree) : tree_(tree) {}

    Vec2 gravity{0, 0};
    float fixedStep = 1.f / 60.f;
    float cellSize = 96.f;
    Layers layers;

    // Advance one fixed step. Collision callbacks are queued and flushed through `dispatch` after the step.
    void step(float dt, const Dispatch& dispatch);

    // Move a body now with solid resolution against static bodies in its mask.
    // Returns which axes were blocked.
    void move(NodeId id, float dx, float dy, bool& hitX, bool& hitY);

    std::vector<NodeId> queryCircle(Vec2 c, float r, uint32_t mask);
    std::vector<NodeId> queryRect(Rect r, uint32_t mask);
    NodeId nearest(Vec2 c, float maxDist, uint32_t mask, NodeId ignore = kNoNode);
    RayHit raycast(Vec2 from, Vec2 to, uint32_t mask, NodeId ignore = kNoNode);

    // Mark cached broadphase data stale (call after creating/destroying/moving colliders outside step()).
    void invalidate() { gridValid_ = false; }
    void reset();
    size_t bodyCount() const { return colliders_.size(); }

private:
    struct Collider {
        NodeId id;
        Vec2 c;        // world centre
        Vec2 half;     // half extents (box) or {r,r}
        float r = 0;
        bool circle = false;
        BodyType type = BodyType::Static;
        uint32_t layer = 1, mask = 0;
    };

    void gather();                        // fill colliders_ from the tree (uses world transforms)
    void buildGrid();
    void ensureGrid();
    static bool overlap(const Collider& a, const Collider& b);
    static bool overlapCircle(const Collider& a, Vec2 c, float r);
    static bool overlapRect(const Collider& a, const Rect& r);
    void resolveAxis(Node& n, Collider& me, float delta, bool xAxis, bool& hit);
    void gatherNear(const Rect& area, std::vector<uint32_t>& out);
    void syncBack(Node& n, const Collider& c);
    struct PairKey {
        NodeId a, b;  // a < b
        bool operator==(const PairKey& o) const { return a == o.a && b == o.b; }
    };
    struct PairHash {
        size_t operator()(const PairKey& k) const { return (size_t)(k.a * 0x9E3779B97F4A7C15ull ^ (k.b + 0x7F4A7C15ull + (k.a << 6))); }
    };
    static PairKey makeKey(NodeId a, NodeId b) { return a < b ? PairKey{a, b} : PairKey{b, a}; }

    SceneTree& tree_;
    std::vector<Collider> colliders_;
    struct Cell { int64_t key; uint32_t idx; };
    std::vector<Cell> grid_;
    bool gridValid_ = false;
    std::vector<uint32_t> stamp_;
    uint32_t stampCounter_ = 0;
    std::vector<uint64_t> candidates_;
    // Active overlap pairs: key -> {idA, idB} ids (full NodeIds for dispatch/validity).
    struct Pair { NodeId a, b; bool aHears, bHears; };
    std::unordered_map<PairKey, Pair, PairHash> active_, next_;
    std::vector<uint32_t> scratch_;
};

}  // namespace aether
