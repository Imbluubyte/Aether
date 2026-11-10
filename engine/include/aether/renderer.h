// Renderer interface + sprite batching. The batch produces quads in *virtual screen space* (after
// the camera has been applied on the CPU), so a backend only needs an orthographic projection.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "aether/types.h"

namespace aether {

using TextureId = uint32_t;  // 0 = none (draws as a solid colour)

struct Vertex {
    float x, y;      // virtual pixels
    float u, v;      // 0..1
    uint32_t color;  // packed RGBA (see Color::packed)
};

struct DrawCmd {
    TextureId texture = 0;
    uint32_t firstQuad = 0;
    uint32_t quadCount = 0;
};

// Collects quads, merging consecutive quads that share a texture into one draw command.
class SpriteBatch {
public:
    void clear() { verts_.clear(); cmds_.clear(); }
    // Corners in order: top-left, top-right, bottom-right, bottom-left.
    void quad(TextureId tex, const Vec2 corners[4], Rect uv, Color c);
    void axisAlignedRect(TextureId tex, float x, float y, float w, float h, Rect uv, Color c);
    void line(Vec2 a, Vec2 b, float thickness, Color c);
    const std::vector<Vertex>& vertices() const { return verts_; }
    const std::vector<DrawCmd>& commands() const { return cmds_; }
    size_t quadCount() const { return verts_.size() / 4; }

private:
    std::vector<Vertex> verts_;
    std::vector<DrawCmd> cmds_;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual const char* name() const = 0;
    // Called once the platform has a valid surface/context (and again after a context loss).
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    // The GPU context was destroyed underneath us (Android surface loss): forget GPU handles without
    // issuing GL calls. init() is called again once a new context exists.
    virtual void contextLost() { shutdown(); }
    // Physical framebuffer size and the letterbox viewport inside it (pixels), plus virtual size.
    virtual void setView(int fbW, int fbH, int vpX, int vpY, int vpW, int vpH, float virtW, float virtH) = 0;
    virtual TextureId createTexture(int w, int h, const uint8_t* rgba, bool smooth) = 0;
    virtual void destroyTexture(TextureId id) = 0;
    virtual void beginFrame(Color clear) = 0;
    virtual void draw(const SpriteBatch& batch) = 0;
    virtual void endFrame() = 0;
    // Statistics for tests / debug overlay.
    virtual uint32_t lastFrameQuads() const { return 0; }
    // Optional readback (software renderer). Returns false if unsupported.
    virtual bool readPixels(int& w, int& h, std::vector<uint8_t>& rgba) const { (void)w; (void)h; (void)rgba; return false; }
};

// Does nothing but count quads. Used for tests that don't need pixels.
class NullRenderer : public Renderer {
public:
    const char* name() const override { return "null"; }
    bool init() override { return true; }
    void shutdown() override {}
    void setView(int, int, int, int, int, int, float, float) override {}
    TextureId createTexture(int, int, const uint8_t*, bool) override { return ++next_; }
    void destroyTexture(TextureId) override {}
    void beginFrame(Color) override {}
    void draw(const SpriteBatch& b) override { quads_ += (uint32_t)b.quadCount(); }
    void endFrame() override { last_ = quads_; quads_ = 0; }
    uint32_t lastFrameQuads() const override { return last_; }

private:
    TextureId next_ = 0;
    uint32_t quads_ = 0, last_ = 0;
};

// CPU rasteriser: renders into an RGBA buffer. Slow but dependency-free; used by the headless
// runner for automated screenshots and for verifying games in CI.
class SoftwareRenderer : public Renderer {
public:
    const char* name() const override { return "software"; }
    bool init() override { return true; }
    void shutdown() override { textures_.clear(); }
    void setView(int fbW, int fbH, int vpX, int vpY, int vpW, int vpH, float virtW, float virtH) override;
    TextureId createTexture(int w, int h, const uint8_t* rgba, bool smooth) override;
    void destroyTexture(TextureId id) override;
    void beginFrame(Color clear) override;
    void draw(const SpriteBatch& batch) override;
    void endFrame() override { last_ = quads_; }
    uint32_t lastFrameQuads() const override { return last_; }
    bool readPixels(int& w, int& h, std::vector<uint8_t>& rgba) const override {
        w = fbW_; h = fbH_; rgba = fb_;
        return true;
    }

private:
    struct Tex { int w = 0, h = 0; std::vector<uint8_t> px; bool smooth = false; bool used = false; };
    void tri(const Vertex& a, const Vertex& b, const Vertex& c, const Tex* tex);
    std::vector<Tex> textures_{1};
    std::vector<uint8_t> fb_;
    int fbW_ = 0, fbH_ = 0, vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0;
    float virtW_ = 1, virtH_ = 1;
    uint32_t quads_ = 0, last_ = 0;
};

}  // namespace aether
