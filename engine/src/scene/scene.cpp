#include "aether/scene.h"

#include <algorithm>
#include <cmath>

namespace aether {

SceneTree::SceneTree() { reset(); }

void SceneTree::reset() {
    slots_.clear();
    freeList_.clear();
    pendingFree_.clear();
    groups_.clear();
    liveCount_ = 0;
    slots_.emplace_back();  // slot 0 is reserved so that NodeId 0 means "none"
    slots_[0].gen = 1;
    root_ = create(NodeType::Node, "root");
}

NodeId SceneTree::create(NodeType type, const std::string& name) {
    uint32_t idx;
    if (!freeList_.empty()) {
        idx = freeList_.back();
        freeList_.pop_back();
    } else {
        idx = (uint32_t)slots_.size();
        slots_.emplace_back();
    }
    Slot& s = slots_[idx];
    s.node = Node();
    s.node.id = makeNodeId(idx, s.gen);
    s.node.type = type;
    s.node.name = name;
    s.node.alive = true;
    if (type == NodeType::Body) s.node.body = BodyType::Static;
    if (type == NodeType::Label) s.node.hasColor = true;
    liveCount_++;
    if (root_ != kNoNode) addChild(root_, s.node.id);
    else updateNodeTransform(s.node);
    return s.node.id;
}

Node* SceneTree::get(NodeId id) {
    uint32_t i = nodeIndex(id);
    if (i == 0 || i >= slots_.size()) return nullptr;
    Slot& s = slots_[i];
    if (!s.node.alive || s.gen != nodeGen(id)) return nullptr;
    return &s.node;
}
const Node* SceneTree::get(NodeId id) const { return const_cast<SceneTree*>(this)->get(id); }

bool SceneTree::valid(NodeId id) const {
    const Node* n = get(id);
    return n && !n->destroyed;
}

void SceneTree::detach(NodeId child) {
    Node* c = get(child);
    if (!c) return;
    if (Node* p = get(c->parent)) {
        auto& v = p->children;
        v.erase(std::remove(v.begin(), v.end(), child), v.end());
    }
    c->parent = kNoNode;
}

bool SceneTree::addChild(NodeId parent, NodeId child) {
    Node* p = get(parent);
    Node* c = get(child);
    if (!p || !c || p->destroyed || c->destroyed || parent == child || child == root_) return false;
    for (NodeId a = parent; a != kNoNode;) {  // reject cycles
        if (a == child) return false;
        const Node* an = get(a);
        a = an ? an->parent : kNoNode;
    }
    detach(child);
    c = get(child);
    p = get(parent);
    c->parent = parent;
    p->children.push_back(child);
    updateNodeTransform(*c);
    return true;
}

NodeId SceneTree::findChild(NodeId parent, const std::string& name, bool recursive) const {
    const Node* p = get(parent);
    if (!p) return kNoNode;
    for (NodeId c : p->children) {
        const Node* cn = get(c);
        if (cn && !cn->destroyed && cn->name == name) return c;
    }
    if (recursive) {
        for (NodeId c : p->children) {
            NodeId r = findChild(c, name, true);
            if (r != kNoNode) return r;
        }
    }
    return kNoNode;
}

void SceneTree::addToGroup(NodeId id, const std::string& g) {
    Node* n = get(id);
    if (!n || inGroup(id, g)) return;
    n->groups.push_back(g);
    groups_[g].push_back(id);
}

void SceneTree::removeFromGroup(NodeId id, const std::string& g) {
    Node* n = get(id);
    if (!n) return;
    n->groups.erase(std::remove(n->groups.begin(), n->groups.end(), g), n->groups.end());
    auto it = groups_.find(g);
    if (it != groups_.end()) it->second.erase(std::remove(it->second.begin(), it->second.end(), id), it->second.end());
}

bool SceneTree::inGroup(NodeId id, const std::string& g) const {
    const Node* n = get(id);
    return n && std::find(n->groups.begin(), n->groups.end(), g) != n->groups.end();
}

std::vector<NodeId> SceneTree::group(const std::string& name) {
    std::vector<NodeId> out;
    auto it = groups_.find(name);
    if (it == groups_.end()) return out;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(), [&](NodeId id) { return !valid(id); }), v.end());
    out = v;
    return out;
}

size_t SceneTree::groupCount(const std::string& name) {
    auto it = groups_.find(name);
    if (it == groups_.end()) return 0;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(), [&](NodeId id) { return !valid(id); }), v.end());
    return v.size();
}

void SceneTree::updateNodeTransform(Node& n) {
    const Node* p = get(n.parent);
    if (!p) { n.worldPos = n.pos; n.worldRot = n.rot; n.worldScale = n.scale; return; }
    float c = std::cos(p->worldRot), s = std::sin(p->worldRot);
    Vec2 sc{n.pos.x * p->worldScale.x, n.pos.y * p->worldScale.y};
    n.worldPos = {p->worldPos.x + sc.x * c - sc.y * s, p->worldPos.y + sc.x * s + sc.y * c};
    n.worldRot = p->worldRot + n.rot;
    n.worldScale = {p->worldScale.x * n.scale.x, p->worldScale.y * n.scale.y};
}

void SceneTree::updateTransforms() {
    struct Item { NodeId id; };
    std::vector<NodeId> stack{root_};
    while (!stack.empty()) {
        NodeId id = stack.back();
        stack.pop_back();
        Node* n = get(id);
        if (!n || n->destroyed) continue;
        const Node* p = get(n->parent);
        if (!p) {
            n->worldPos = n->pos;
            n->worldRot = n->rot;
            n->worldScale = n->scale;
        } else {
            float c = std::cos(p->worldRot), s = std::sin(p->worldRot);
            Vec2 sc{n->pos.x * p->worldScale.x, n->pos.y * p->worldScale.y};
            n->worldPos = {p->worldPos.x + sc.x * c - sc.y * s, p->worldPos.y + sc.x * s + sc.y * c};
            n->worldRot = p->worldRot + n->rot;
            n->worldScale = {p->worldScale.x * n->scale.x, p->worldScale.y * n->scale.y};
        }
        for (NodeId c : n->children) stack.push_back(c);
    }
}

std::vector<NodeId> SceneTree::collectGarbage() {
    std::vector<NodeId> freed;
    freed.swap(pendingFree_);
    for (NodeId id : freed) {
        uint32_t i = nodeIndex(id);
        if (i == 0 || i >= slots_.size()) continue;
        Slot& s = slots_[i];
        if (!s.node.alive || s.gen != nodeGen(id)) continue;
        for (auto& g : s.node.groups) {
            auto it = groups_.find(g);
            if (it != groups_.end()) it->second.erase(std::remove(it->second.begin(), it->second.end(), id), it->second.end());
        }
        s.node = Node();
        s.gen++;
        if (s.gen == 0) s.gen = 1;
        freeList_.push_back(i);
        liveCount_--;
    }
    return freed;
}

const std::vector<NodeId>& SceneTree::allSlotsSnapshot(std::vector<NodeId>& out) const {
    out.clear();
    for (size_t i = 1; i < slots_.size(); i++)
        if (slots_[i].node.alive && !slots_[i].node.destroyed) out.push_back(slots_[i].node.id);
    return out;
}

}  // namespace aether
