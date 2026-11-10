// OpenGL ES 3.0 renderer. Shared by the Android backend and the future Nintendo Switch backend
// (devkitPro's libnx exposes EGL + GLES via mesa). Only compiled when AETHER_RENDERER_GLES is set.
#pragma once
#include "aether/renderer.h"

namespace aether {

class GLESRenderer : public Renderer {
public:
    ~GLESRenderer() override;
    const char* name() const override { return "gles3"; }
    bool init() override;
    void shutdown() override;
    void setView(int fbW, int fbH, int vpX, int vpY, int vpW, int vpH, float virtW, float virtH) override;
    TextureId createTexture(int w, int h, const uint8_t* rgba, bool smooth) override;
    void destroyTexture(TextureId id) override;
    void beginFrame(Color clear) override;
    void draw(const SpriteBatch& batch) override;
    void endFrame() override;
    uint32_t lastFrameQuads() const override { return last_; }

private:
    unsigned program_ = 0, vbo_ = 0, ibo_ = 0, vao_ = 0, whiteTex_ = 0;
    int uVirtual_ = -1, uTex_ = -1;
    int fbW_ = 0, fbH_ = 0, vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0;
    float virtW_ = 1, virtH_ = 1;
    uint32_t quads_ = 0, last_ = 0;
    bool ready_ = false;
};

}  // namespace aether
