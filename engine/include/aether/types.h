// Aether core value types. Header-only, no dependencies beyond the STL.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace aether {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    // 0xRRGGBBAA
    static Color fromRGBA8(uint32_t v) {
        return {((v >> 24) & 255) / 255.f, ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f};
    }
    // Packed as little-endian RGBA bytes (R in the lowest byte) - the layout GL expects for GL_UNSIGNED_BYTE.
    uint32_t packed() const {
        auto c = [](float f) { return (uint32_t)(f < 0 ? 0 : (f > 1 ? 255 : f * 255.f + 0.5f)); };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

// Handles into the scene tree. Low 32 bits = slot index, high 32 bits = generation. 0 is "no node".
using NodeId = uint64_t;
constexpr NodeId kNoNode = 0;
inline uint32_t nodeIndex(NodeId id) { return (uint32_t)(id & 0xffffffffu); }
inline uint32_t nodeGen(NodeId id) { return (uint32_t)(id >> 32); }
inline NodeId makeNodeId(uint32_t index, uint32_t gen) { return ((NodeId)gen << 32) | index; }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace aether
