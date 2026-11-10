#include "aether/physics.h"

#include <algorithm>
#include <cmath>

namespace aether {

// ---------------------------------------------------------------- layers

void Layers::set(const std::vector<std::string>& names) {
    names_.clear();
    for (size_t i = 0; i < names.size() && i < 32; i++) names_.push_back(names[i]);
}

bool Layers::bitFor(const std::string& name, uint32_t& bit) const {
    for (size_t i = 0; i < names_.size(); i++)
        if (names_[i] == name) { bit = 1u << i; return true; }
    return false;
}

std::string Layers::nameFor(uint32_t bit) const {
    for (size_t i = 0; i < names_.size(); i++)
        if (bit == (1u << i)) return names_[i];
    return "";
}

// ---------------------------------------------------------------- helpers

static constexpr float kEps = 1e-3f;

static Vec2 worldDeltaToLocal(const SceneTree& t, const Node& n, Vec2 d) {
    const Node* p = t.get(n.parent);
    if (!p) return d;
    float c = std::cos(-p->worldRot), s = std::sin(-p->worldRot);
    Vec2 r{d.x * c - d.y * s, d.x * s + d.y * c};
    if (std::fabs(p->worldScale.x) > 1e-6f) r.x /= p->worldScale.x;
    if (std::fabs(p->worldScale.y) > 1e-6f) r.y /= p->worldScale.y;
    return r;
}

static bool makeCollider(const Node& n, float& cx, float& cy, Vec2& half, float& r, bool& circle) {
    Vec2 size = n.baseSize();
    float sx = std::fabs(n.worldScale.x), sy = std::fabs(n.worldScale.y);
    cx = n.worldPos.x;
    cy = n.worldPos.y;
    if (n.circle || n.radius > 0) {
        float rr = n.radius > 0 ? n.radius : std::min(size.x, size.y) * 0.5f;
        r = rr * std::max(sx, sy);
        half = {r, r};
        circle = true;
        return r > 0;
    }
    circle = false;
    r = 0;
    half = {size.x * sx * 0.5f, size.y * sy * 0.5f};
    return half.x > 0 && half.y > 0;
}

void Physics::reset() {
    colliders_.clear();
    grid_.clear();
    active_.clear();
    next_.clear();
    gridValid_ = false;
}

void Physics::gather() {
    colliders_.clear();
    tree_.traverse([&](Node& n) {
        if (n.type != NodeType::Body || !n.bodyActive) return;
        Collider c;
        if (!makeCollider(n, c.c.x, c.c.y, c.half, c.r, c.circle)) return;
        c.id = n.id;
        c.type = n.body;
        c.layer = n.layer;
        c.mask = n.mask;
        colliders_.push_back(c);
    });
}

static int64_t cellKey(int32_t cx, int32_t cy) { return (int64_t)(((uint64_t)(uint32_t)cx << 32) | (uint64_t)(uint32_t)cy); }

void Physics::buildGrid() {
    gather();
    grid_.clear();
    float inv = 1.f / cellSize;
    for (uint32_t i = 0; i < colliders_.size(); i++) {
        const Collider& c = colliders_[i];
        int x0 = (int)std::floor((c.c.x - c.half.x) * inv), x1 = (int)std::floor((c.c.x + c.half.x) * inv);
        int y0 = (int)std::floor((c.c.y - c.half.y) * inv), y1 = (int)std::floor((c.c.y + c.half.y) * inv);
        // Guard against pathological huge colliders blowing up the grid.
        if ((int64_t)(x1 - x0 + 1) * (y1 - y0 + 1) > 4096) { x1 = x0 + 63; y1 = y0 + 63; }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) grid_.push_back({cellKey(x, y), i});
    }
    std::sort(grid_.begin(), grid_.end(), [](const Cell& a, const Cell& b) { return a.key != b.key ? a.key < b.key : a.idx < b.idx; });
    if (stamp_.size() < colliders_.size()) stamp_.resize(colliders_.size(), 0);
    gridValid_ = true;
}

void Physics::ensureGrid() {
    if (!gridValid_) buildGrid();
}

void Physics::gatherNear(const Rect& a, std::vector<uint32_t>& out) {
    out.clear();
    float inv = 1.f / cellSize;
    int x0 = (int)std::floor(a.x * inv), x1 = (int)std::floor((a.x + a.w) * inv);
    int y0 = (int)std::floor(a.y * inv), y1 = (int)std::floor((a.y + a.h) * inv);
    if ((int64_t)(x1 - x0 + 1) * (y1 - y0 + 1) > 4096) {  // huge area: just test everything
        for (uint32_t i = 0; i < colliders_.size(); i++) out.push_back(i);
        return;
    }
    if (++stampCounter_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0); stampCounter_ = 1; }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int64_t key = cellKey(x, y);
            auto it = std::lower_bound(grid_.begin(), grid_.end(), key, [](const Cell& c, int64_t k) { return c.key < k; });
            for (; it != grid_.end() && it->key == key; ++it)
                if (stamp_[it->idx] != stampCounter_) { stamp_[it->idx] = stampCounter_; out.push_back(it->idx); }
        }
}

// ---------------------------------------------------------------- overlap tests

bool Physics::overlap(const Collider& a, const Collider& b) {
    if (a.circle && b.circle) {
        float dx = a.c.x - b.c.x, dy = a.c.y - b.c.y, rr = a.r + b.r;
        return dx * dx + dy * dy < rr * rr;
    }
    if (!a.circle && !b.circle)
        return std::fabs(a.c.x - b.c.x) < a.half.x + b.half.x && std::fabs(a.c.y - b.c.y) < a.half.y + b.half.y;
    const Collider& circ = a.circle ? a : b;
    const Collider& box = a.circle ? b : a;
    float px = clampf(circ.c.x, box.c.x - box.half.x, box.c.x + box.half.x);
    float py = clampf(circ.c.y, box.c.y - box.half.y, box.c.y + box.half.y);
    float dx = circ.c.x - px, dy = circ.c.y - py;
    return dx * dx + dy * dy < circ.r * circ.r;
}

bool Physics::overlapCircle(const Collider& a, Vec2 c, float r) {
    if (a.circle) {
        float dx = a.c.x - c.x, dy = a.c.y - c.y, rr = a.r + r;
        return dx * dx + dy * dy <= rr * rr;
    }
    float px = clampf(c.x, a.c.x - a.half.x, a.c.x + a.half.x);
    float py = clampf(c.y, a.c.y - a.half.y, a.c.y + a.half.y);
    float dx = c.x - px, dy = c.y - py;
    return dx * dx + dy * dy <= r * r;
}

bool Physics::overlapRect(const Collider& a, const Rect& r) {
    float rx = r.x + r.w * 0.5f, ry = r.y + r.h * 0.5f;
    if (a.circle) {
        float px = clampf(a.c.x, r.x, r.x + r.w), py = clampf(a.c.y, r.y, r.y + r.h);
        float dx = a.c.x - px, dy = a.c.y - py;
        return dx * dx + dy * dy <= a.r * a.r;
    }
    return std::fabs(a.c.x - rx) <= a.half.x + r.w * 0.5f && std::fabs(a.c.y - ry) <= a.half.y + r.h * 0.5f;
}

// ---------------------------------------------------------------- solid movement

// Moves `me` along one axis by `delta`, stopping against solid static bodies in me.mask.
// Applies the achieved displacement to the node's local position.
void Physics::resolveAxis(Node& n, Collider& me, float delta, bool xAxis, bool& hit) {
    if (delta == 0) return;
    float& pos = xAxis ? me.c.x : me.c.y;
    float start = pos;
    pos += delta;
    Rect area{me.c.x - me.half.x - std::fabs(delta), me.c.y - me.half.y - std::fabs(delta), me.half.x * 2 + std::fabs(delta) * 2,
              me.half.y * 2 + std::fabs(delta) * 2};
    // Ensure we also catch statics at the *start* position; area above is centred on the new position
    // and padded by |delta| so it covers both.
    gatherNear(area, scratch_);
    for (uint32_t idx : scratch_) {
        const Collider& s = colliders_[idx];
        if (s.type != BodyType::Static || s.id == me.id || !(me.mask & s.layer)) continue;
        if (!(std::fabs(me.c.x - s.c.x) < me.half.x + s.half.x - kEps && std::fabs(me.c.y - s.c.y) < me.half.y + s.half.y - kEps)) continue;
        hit = true;
        if (xAxis) pos = delta > 0 ? s.c.x - s.half.x - me.half.x : s.c.x + s.half.x + me.half.x;
        else pos = delta > 0 ? s.c.y - s.half.y - me.half.y : s.c.y + s.half.y + me.half.y;
    }
    float actual = pos - start;
    Vec2 d = xAxis ? Vec2{actual, 0} : Vec2{0, actual};
    n.worldPos += d;
    n.pos += worldDeltaToLocal(tree_, n, d);
}

void Physics::move(NodeId id, float dx, float dy, bool& hitX, bool& hitY) {
    hitX = hitY = false;
    Node* n = tree_.get(id);
    if (!n || n->destroyed) return;
    if (n->type != NodeType::Body || !n->bodyActive) {  // plain translate
        Vec2 d{dx, dy};
        n->pos += worldDeltaToLocal(tree_, *n, d);
        n->worldPos += d;
        return;
    }
    ensureGrid();
    Collider me;
    if (!makeCollider(*n, me.c.x, me.c.y, me.half, me.r, me.circle)) {
        Vec2 d{dx, dy};
        n->pos += worldDeltaToLocal(tree_, *n, d);
        n->worldPos += d;
        return;
    }
    me.id = id; me.type = n->body; me.layer = n->layer; me.mask = n->mask;
    resolveAxis(*n, me, dx, true, hitX);
    resolveAxis(*n, me, dy, false, hitY);
    gridValid_ = gridValid_ && true;  // statics don't move; moving bodies are only stale in the grid
}

// ---------------------------------------------------------------- step

void Physics::step(float dt, const Dispatch& dispatch) {
    ensureGrid();  // statics/solids for movement
    // Integrate
    std::vector<NodeId> bodies;
    tree_.traverse([&](Node& n) {
        if (n.type == NodeType::Body && n.bodyActive) bodies.push_back(n.id);
    });
    for (NodeId id : bodies) {
        Node* n = tree_.get(id);
        if (!n || n->destroyed) continue;
        if (n->body == BodyType::Dynamic) {
            n->vel += gravity * (n->gravityScale * dt);
            n->onFloor = n->onCeiling = n->onWall = false;
            bool hx = false, hy = false;
            Vec2 d = n->vel * dt;
            Collider me;
            if (makeCollider(*n, me.c.x, me.c.y, me.half, me.r, me.circle)) {
                me.id = id; me.type = n->body; me.layer = n->layer; me.mask = n->mask;
                resolveAxis(*n, me, d.x, true, hx);
                resolveAxis(*n, me, d.y, false, hy);
            } else {
                n->pos += worldDeltaToLocal(tree_, *n, d);
                n->worldPos += d;
            }
            if (hx) { n->vel.x = 0; n->onWall = true; }
            if (hy) {
                if (d.y > 0) n->onFloor = true; else n->onCeiling = true;
                n->vel.y = 0;
            }
        } else if (n->body == BodyType::Kinematic || n->body == BodyType::Trigger) {
            Vec2 d = n->vel * dt;
            if (d.x != 0 || d.y != 0) {
                n->pos += worldDeltaToLocal(tree_, *n, d);
                n->worldPos += d;
            }
        }
    }

    // Broadphase with up-to-date positions.
    gridValid_ = false;
    ensureGrid();
    candidates_.clear();
    for (size_t i = 0; i < grid_.size();) {
        size_t j = i;
        while (j < grid_.size() && grid_[j].key == grid_[i].key) j++;
        for (size_t a = i; a < j; a++)
            for (size_t b = a + 1; b < j; b++) {
                uint32_t ia = grid_[a].idx, ib = grid_[b].idx;
                if (ia > ib) std::swap(ia, ib);
                candidates_.push_back(((uint64_t)ia << 32) | ib);
            }
        i = j;
    }
    std::sort(candidates_.begin(), candidates_.end());
    candidates_.erase(std::unique(candidates_.begin(), candidates_.end()), candidates_.end());

    next_.clear();
    for (uint64_t k : candidates_) {
        const Collider& a = colliders_[(uint32_t)(k >> 32)];
        const Collider& b = colliders_[(uint32_t)(k & 0xffffffffu)];
        if (a.type == BodyType::Static && b.type == BodyType::Static) continue;
        bool aHears = (a.mask & b.layer) != 0, bHears = (b.mask & a.layer) != 0;
        if (!aHears && !bHears) continue;
        if (!overlap(a, b)) continue;
        next_[makeKey(a.id, b.id)] = Pair{a.id, b.id, aHears, bHears};
    }

    // Diff against the previous step to produce enter/exit events.
    struct Ev { NodeId self, other; bool enter; };
    std::vector<Ev> events;
    for (auto& kv : next_) {
        if (active_.find(kv.first) != active_.end()) continue;
        const Pair& p = kv.second;
        if (p.aHears) events.push_back({p.a, p.b, true});
        if (p.bHears) events.push_back({p.b, p.a, true});
    }
    for (auto& kv : active_) {
        if (next_.find(kv.first) != next_.end()) continue;
        const Pair& p = kv.second;
        if (!tree_.valid(p.a) || !tree_.valid(p.b)) continue;
        if (p.aHears) events.push_back({p.a, p.b, false});
        if (p.bHears) events.push_back({p.b, p.a, false});
    }
    active_.swap(next_);
    next_.clear();
    if (dispatch)
        for (const Ev& e : events) dispatch(e.self, e.other, e.enter);
}

// ---------------------------------------------------------------- queries

std::vector<NodeId> Physics::queryCircle(Vec2 c, float r, uint32_t mask) {
    ensureGrid();
    gatherNear({c.x - r, c.y - r, r * 2, r * 2}, scratch_);
    std::vector<NodeId> out;
    for (uint32_t idx : scratch_) {
        const Collider& col = colliders_[idx];
        if ((col.layer & mask) && overlapCircle(col, c, r)) out.push_back(col.id);
    }
    return out;
}

std::vector<NodeId> Physics::queryRect(Rect r, uint32_t mask) {
    ensureGrid();
    gatherNear(r, scratch_);
    std::vector<NodeId> out;
    for (uint32_t idx : scratch_) {
        const Collider& col = colliders_[idx];
        if ((col.layer & mask) && overlapRect(col, r)) out.push_back(col.id);
    }
    return out;
}

NodeId Physics::nearest(Vec2 c, float maxDist, uint32_t mask, NodeId ignore) {
    ensureGrid();
    gatherNear({c.x - maxDist, c.y - maxDist, maxDist * 2, maxDist * 2}, scratch_);
    NodeId best = kNoNode;
    float bestD = maxDist * maxDist;
    for (uint32_t idx : scratch_) {
        const Collider& col = colliders_[idx];
        if (!(col.layer & mask) || col.id == ignore) continue;
        float dx = col.c.x - c.x, dy = col.c.y - c.y, d = dx * dx + dy * dy;
        if (d <= bestD) { bestD = d; best = col.id; }
    }
    return best;
}

RayHit Physics::raycast(Vec2 from, Vec2 to, uint32_t mask, NodeId ignore) {
    ensureGrid();
    RayHit best;
    best.distance = 1e30f;
    float minx = std::min(from.x, to.x), miny = std::min(from.y, to.y);
    gatherNear({minx, miny, std::fabs(to.x - from.x), std::fabs(to.y - from.y)}, scratch_);
    Vec2 d = to - from;
    float len = d.length();
    if (len <= 0) return RayHit{};
    for (uint32_t idx : scratch_) {
        const Collider& col = colliders_[idx];
        if (!(col.layer & mask) || col.id == ignore) continue;
        float t = -1;
        Vec2 normal;
        if (col.circle) {
            Vec2 f = from - col.c;
            float a = d.lengthSq(), b = 2 * (f.x * d.x + f.y * d.y), c = f.lengthSq() - col.r * col.r;
            float disc = b * b - 4 * a * c;
            if (disc >= 0) {
                float sq = std::sqrt(disc), t0 = (-b - sq) / (2 * a), t1 = (-b + sq) / (2 * a);
                if (t0 >= 0 && t0 <= 1) t = t0;
                else if (t0 < 0 && t1 >= 0) t = 0;  // started inside
                if (t >= 0) {
                    Vec2 p = from + d * t;
                    Vec2 nn = p - col.c;
                    float nl = nn.length();
                    normal = nl > 0 ? nn * (1.f / nl) : Vec2{0, -1};
                }
            }
        } else {
            float tmin = 0, tmax = 1;
            Vec2 nrm{0, 0};
            bool ok = true;
            for (int axis = 0; axis < 2 && ok; axis++) {
                float o = axis ? from.y : from.x, dd = axis ? d.y : d.x;
                float lo = (axis ? col.c.y - col.half.y : col.c.x - col.half.x);
                float hi = (axis ? col.c.y + col.half.y : col.c.x + col.half.x);
                if (std::fabs(dd) < 1e-9f) {
                    if (o < lo || o > hi) ok = false;
                } else {
                    float t0 = (lo - o) / dd, t1 = (hi - o) / dd;
                    float sign = -1;
                    if (t0 > t1) { std::swap(t0, t1); sign = 1; }
                    if (t0 > tmin) { tmin = t0; nrm = axis ? Vec2{0, sign} : Vec2{sign, 0}; }
                    tmax = std::min(tmax, t1);
                    if (tmin > tmax) ok = false;
                }
            }
            if (ok) { t = tmin; normal = nrm; }
        }
        if (t >= 0 && t * len < best.distance) {
            best.hit = true;
            best.node = col.id;
            best.distance = t * len;
            best.point = from + d * t;
            best.normal = normal;
        }
    }
    if (!best.hit) best.distance = 0;
    return best;
}

}  // namespace aether
