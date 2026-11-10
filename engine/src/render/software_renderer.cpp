#include <algorithm>
#include <cmath>
#include <cstring>

#include "aether/renderer.h"

namespace aether {

void SoftwareRenderer::setView(int fbW, int fbH, int vpX, int vpY, int vpW, int vpH, float virtW, float virtH) {
    if (fbW != fbW_ || fbH != fbH_) {
        fbW_ = fbW; fbH_ = fbH;
        fb_.assign((size_t)fbW * (size_t)fbH * 4, 0);
    }
    vpX_ = vpX; vpY_ = vpY; vpW_ = vpW; vpH_ = vpH;
    virtW_ = virtW; virtH_ = virtH;
}

TextureId SoftwareRenderer::createTexture(int w, int h, const uint8_t* rgba, bool smooth) {
    Tex t;
    t.w = w; t.h = h; t.smooth = smooth; t.used = true;
    t.px.assign(rgba, rgba + (size_t)w * (size_t)h * 4);
    for (size_t i = 1; i < textures_.size(); i++)
        if (!textures_[i].used) { textures_[i] = std::move(t); return (TextureId)i; }
    textures_.push_back(std::move(t));
    return (TextureId)(textures_.size() - 1);
}

void SoftwareRenderer::destroyTexture(TextureId id) {
    if (id > 0 && id < textures_.size()) textures_[id] = Tex();
}

void SoftwareRenderer::beginFrame(Color c) {
    uint32_t p = c.packed();
    for (size_t i = 0; i + 3 < fb_.size(); i += 4) std::memcpy(&fb_[i], &p, 4);
    quads_ = 0;
}

static inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Top-left fill rule: a pixel centre exactly on an edge belongs to only one of the two triangles
// sharing it, so translucent quads are never blended twice along their diagonal.
static inline bool topLeft(float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    return dy < 0 || (dy == 0 && dx > 0);
}

void SoftwareRenderer::tri(const Vertex& a0, const Vertex& b0, const Vertex& c0, const Tex* tex) {
    float sx = vpW_ / virtW_, sy = vpH_ / virtH_;
    const Vertex* va = &a0; const Vertex* vb = &b0; const Vertex* vc = &c0;
    float ax = va->x * sx + vpX_, ay = va->y * sy + vpY_;
    float bx = vb->x * sx + vpX_, by = vb->y * sy + vpY_;
    float cx = vc->x * sx + vpX_, cy = vc->y * sy + vpY_;
    float area = edge(ax, ay, bx, by, cx, cy);
    if (std::fabs(area) < 1e-6f) return;
    if (area < 0) {  // normalise winding so the edge test below is consistent
        std::swap(vb, vc);
        std::swap(bx, cx); std::swap(by, cy);
        area = -area;
    }
    const bool tl0 = topLeft(bx, by, cx, cy), tl1 = topLeft(cx, cy, ax, ay), tl2 = topLeft(ax, ay, bx, by);
    int minX = std::max((int)std::floor(std::min({ax, bx, cx})), std::max(0, vpX_));
    int maxX = std::min((int)std::ceil(std::max({ax, bx, cx})), std::min(fbW_, vpX_ + vpW_) - 1);
    int minY = std::max((int)std::floor(std::min({ay, by, cy})), std::max(0, vpY_));
    int maxY = std::min((int)std::ceil(std::max({ay, by, cy})), std::min(fbH_, vpY_ + vpH_) - 1);
    float inv = 1.f / area;
    auto chan = [](uint32_t col, int s) { return ((col >> s) & 255) / 255.f; };
    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = edge(bx, by, cx, cy, px, py), e1 = edge(cx, cy, ax, ay, px, py), e2 = edge(ax, ay, bx, by, px, py);
            if (e0 < 0 || (e0 == 0 && !tl0) || e1 < 0 || (e1 == 0 && !tl1) || e2 < 0 || (e2 == 0 && !tl2)) continue;
            float w0 = e0 * inv, w1 = e1 * inv, w2 = e2 * inv;
            float u = va->u * w0 + vb->u * w1 + vc->u * w2, v = va->v * w0 + vb->v * w1 + vc->v * w2;
            float r = chan(va->color, 0), g = chan(va->color, 8), bl = chan(va->color, 16), al = chan(va->color, 24);
            if (tex && tex->w > 0) {
                int tx = std::min(tex->w - 1, std::max(0, (int)std::floor(u * tex->w)));
                int ty = std::min(tex->h - 1, std::max(0, (int)std::floor(v * tex->h)));
                const uint8_t* t = &tex->px[((size_t)ty * (size_t)tex->w + (size_t)tx) * 4];
                r *= t[0] / 255.f; g *= t[1] / 255.f; bl *= t[2] / 255.f; al *= t[3] / 255.f;
            }
            if (al <= 0) continue;
            uint8_t* d = &fb_[((size_t)y * (size_t)fbW_ + (size_t)x) * 4];
            d[0] = (uint8_t)(r * al * 255.f + d[0] * (1 - al) + 0.5f);
            d[1] = (uint8_t)(g * al * 255.f + d[1] * (1 - al) + 0.5f);
            d[2] = (uint8_t)(bl * al * 255.f + d[2] * (1 - al) + 0.5f);
            d[3] = 255;
        }
    }
}

void SoftwareRenderer::draw(const SpriteBatch& batch) {
    const auto& v = batch.vertices();
    for (const DrawCmd& cmd : batch.commands()) {
        const Tex* tex = (cmd.texture > 0 && cmd.texture < textures_.size() && textures_[cmd.texture].used) ? &textures_[cmd.texture] : nullptr;
        for (uint32_t q = 0; q < cmd.quadCount; q++) {
            const Vertex* p = &v[(size_t)(cmd.firstQuad + q) * 4];
            tri(p[0], p[1], p[2], tex);
            tri(p[0], p[2], p[3], tex);
            quads_++;
        }
    }
}

}  // namespace aether
