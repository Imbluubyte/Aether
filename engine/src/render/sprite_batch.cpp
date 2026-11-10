#include <cmath>

#include "aether/renderer.h"

namespace aether {

void SpriteBatch::quad(TextureId tex, const Vec2 c[4], Rect uv, Color col) {
    uint32_t packed = col.packed();
    float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
    verts_.push_back({c[0].x, c[0].y, u0, v0, packed});
    verts_.push_back({c[1].x, c[1].y, u1, v0, packed});
    verts_.push_back({c[2].x, c[2].y, u1, v1, packed});
    verts_.push_back({c[3].x, c[3].y, u0, v1, packed});
    uint32_t qi = (uint32_t)(verts_.size() / 4 - 1);
    if (!cmds_.empty() && cmds_.back().texture == tex) cmds_.back().quadCount++;
    else cmds_.push_back({tex, qi, 1});
}

void SpriteBatch::axisAlignedRect(TextureId tex, float x, float y, float w, float h, Rect uv, Color c) {
    Vec2 k[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    quad(tex, k, uv, c);
}

void SpriteBatch::line(Vec2 a, Vec2 b, float t, Color c) {
    Vec2 d = b - a;
    float len = d.length();
    if (len <= 0) return;
    Vec2 n{-d.y / len * t * 0.5f, d.x / len * t * 0.5f};
    Vec2 k[4] = {a + n, b + n, b - n, a - n};
    quad(0, k, Rect{0, 0, 1, 1}, c);
}

}  // namespace aether
