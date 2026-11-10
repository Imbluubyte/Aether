// Scene tree: nodes live in a slot pool addressed by generational NodeId handles, so scripts can
// hold references to nodes that are later destroyed without ever dereferencing freed memory.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "aether/types.h"

namespace aether {

enum class NodeType : uint8_t { Node, Sprite, AnimatedSprite, Body, Label };
enum class BodyType : uint8_t { Static, Dynamic, Kinematic, Trigger };

struct Animation {
    std::string name;
    std::vector<int> frames;  // 0-based cell indices into the spritesheet grid
    float fps = 8;
    bool loop = true;
};

struct Node {
    // identity / tree
    NodeId id = kNoNode;
    NodeType type = NodeType::Node;
    std::string name;
    NodeId parent = kNoNode;
    std::vector<NodeId> children;
    std::vector<std::string> groups;
    bool alive = false;     // slot in use
    bool destroyed = false; // destroy() called; freed at end of frame

    // transform (local)
    Vec2 pos;
    float rot = 0;          // radians
    Vec2 scale{1, 1};
    Vec2 worldPos;          // cached by SceneTree::updateTransforms
    float worldRot = 0;
    Vec2 worldScale{1, 1};

    // rendering
    bool visible = true;
    bool screenSpace = false;   // ignores the camera (HUD)
    int z = 0;
    float w = 0, h = 0;         // size in world units; 0 = texture/region size (also the box collider size)
    Color color;
    bool hasColor = false;
    std::string texturePath;
    uint32_t texture = 0;       // renderer texture id, resolved lazily
    int texW = 0, texH = 0;
    Rect region{0, 0, 0, 0};    // pixel sub-rect; w==0 means whole texture
    bool flipX = false, flipY = false;
    Vec2 origin{0.5f, 0.5f};

    // animation (spritesheet grid)
    int frameW = 0, frameH = 0;
    std::vector<Animation> animations;
    int animIndex = -1;
    int animFrame = 0;          // index into animations[animIndex].frames
    float animTime = 0;
    float animSpeed = 1;
    bool animPlaying = false;
    int cell = 0;               // current grid cell when no animation is playing

    // label
    std::string text;
    float textSize = 2;         // pixel multiplier of the 5x7 font
    int align = 0;              // 0 left, 1 center, 2 right

    // physics
    BodyType body = BodyType::Static;
    bool circle = false;
    float radius = 0;
    uint32_t layer = 1;
    uint32_t mask = 0xffffffffu;
    Vec2 vel;
    float gravityScale = 1;
    bool onFloor = false, onCeiling = false, onWall = false;
    bool bodyActive = true;

    // scripting
    std::string scriptPath;
    bool hasScript = false;

    // Unscaled size in world units: explicit w/h, else animation cell / region / texture size.
    Vec2 baseSize() const {
        float bw = w, bh = h;
        if (bw <= 0) bw = frameW > 0 ? (float)frameW : (region.w > 0 ? region.w : (float)texW);
        if (bh <= 0) bh = frameH > 0 ? (float)frameH : (region.h > 0 ? region.h : (float)texH);
        return {bw, bh};
    }
};

class SceneTree {
public:
    SceneTree();

    NodeId create(NodeType type, const std::string& name = "");
    // Marks the node (and its subtree) destroyed; they are unlinked now and freed by collectGarbage().
    // `onDestroy` (optional) is invoked for each node before it is flagged, children first.
    template <typename F>
    void destroy(NodeId id, F&& onDestroy) {
        Node* n = get(id);
        if (!n || n->destroyed) return;
        std::vector<NodeId> kids = n->children;
        for (NodeId c : kids) destroy(c, onDestroy);
        n = get(id);
        if (!n || n->destroyed) return;
        onDestroy(id);
        n = get(id);
        if (!n) return;
        n->destroyed = true;
        detach(id);
        pendingFree_.push_back(id);
    }
    void destroy(NodeId id) { destroy(id, [](NodeId) {}); }

    // True only for nodes that exist and have not been destroyed.
    bool valid(NodeId id) const;
    Node* get(NodeId id);
    const Node* get(NodeId id) const;

    bool addChild(NodeId parent, NodeId child);  // reparents; rejects cycles
    void detach(NodeId child);
    NodeId root() const { return root_; }
    NodeId findChild(NodeId parent, const std::string& name, bool recursive) const;

    void addToGroup(NodeId id, const std::string& group);
    void removeFromGroup(NodeId id, const std::string& group);
    bool inGroup(NodeId id, const std::string& group) const;
    // Returns live members of the group (also prunes dead ones).
    std::vector<NodeId> group(const std::string& name);
    size_t groupCount(const std::string& name);

    // Recompute world transforms for the whole tree (top-down).
    void updateTransforms();
    // Recompute just this node's cached world transform from its parent's (children stay stale until
    // the next updateTransforms). Makes freshly created / moved nodes immediately queryable.
    void updateNodeTransform(Node& n);
    // Free nodes destroyed this frame. Returns the freed ids so callers can drop their own references.
    std::vector<NodeId> collectGarbage();

    // Visit all live nodes in depth-first draw order.
    template <typename F>
    void traverse(F&& f) {
        std::vector<NodeId> stack{root_};
        while (!stack.empty()) {
            NodeId id = stack.back();
            stack.pop_back();
            Node* n = get(id);
            if (!n || n->destroyed) continue;
            f(*n);
            for (size_t i = n->children.size(); i-- > 0;) stack.push_back(n->children[i]);
        }
    }

    size_t liveCount() const { return liveCount_; }
    // Drop everything and recreate an empty root (used when changing scenes / restarting).
    void reset();
    const std::vector<NodeId>& allSlotsSnapshot(std::vector<NodeId>& out) const;

private:
    struct Slot { Node node; uint32_t gen = 1; };
    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;
    std::vector<NodeId> pendingFree_;
    std::unordered_map<std::string, std::vector<NodeId>> groups_;
    NodeId root_ = kNoNode;
    size_t liveCount_ = 0;
};

}  // namespace aether
