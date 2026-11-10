#ifdef AETHER_RENDERER_GLES
#include "aether/gles_renderer.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "aether/log.h"

namespace aether {

static constexpr uint32_t kMaxQuadsPerDraw = 8192;

static const char* kVert = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform vec2 uVirtual;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uVirtual.x * 2.0 - 1.0, 1.0 - aPos.y / uVirtual.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

static const char* kFrag = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 outColor;
void main() { outColor = texture(uTex, vUV) * vColor; }
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof buf, nullptr, buf);
        log::errorf("shader compile failed: %s", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLESRenderer::~GLESRenderer() { shutdown(); }

bool GLESRenderer::init() {
    shutdown();
    GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) { log::errorf("shader link failed"); return false; }
    uVirtual_ = glGetUniformLocation(program_, "uVirtual");
    uTex_ = glGetUniformLocation(program_, "uTex");

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(kMaxQuadsPerDraw * 4 * sizeof(Vertex)), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    std::vector<uint16_t> idx(kMaxQuadsPerDraw * 6);
    for (uint32_t q = 0; q < kMaxQuadsPerDraw; q++) {
        uint16_t b = (uint16_t)(q * 4);
        uint16_t* p = &idx[q * 6];
        p[0] = b; p[1] = (uint16_t)(b + 1); p[2] = (uint16_t)(b + 2);
        p[3] = b; p[4] = (uint16_t)(b + 2); p[5] = (uint16_t)(b + 3);
    }
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * sizeof(uint16_t)), idx.data(), GL_STATIC_DRAW);

    const uint8_t white[4] = {255, 255, 255, 255};
    whiteTex_ = createTexture(1, 1, white, false);
    ready_ = true;
    log::infof("GLES renderer ready: %s", (const char*)glGetString(GL_RENDERER));
    return true;
}

void GLESRenderer::shutdown() {
    if (!ready_ && !program_) return;
    if (program_) glDeleteProgram(program_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (whiteTex_) { GLuint t = whiteTex_; glDeleteTextures(1, &t); }
    program_ = vbo_ = ibo_ = vao_ = whiteTex_ = 0;
    ready_ = false;
}

void GLESRenderer::setView(int fbW, int fbH, int vpX, int vpY, int vpW, int vpH, float virtW, float virtH) {
    fbW_ = fbW; fbH_ = fbH; vpX_ = vpX; vpY_ = vpY; vpW_ = vpW; vpH_ = vpH; virtW_ = virtW; virtH_ = virtH;
}

TextureId GLESRenderer::createTexture(int w, int h, const uint8_t* rgba, bool smooth) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GLint f = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return (TextureId)t;
}

void GLESRenderer::destroyTexture(TextureId id) {
    GLuint t = id;
    if (t) glDeleteTextures(1, &t);
}

void GLESRenderer::beginFrame(Color c) {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fbW_, fbH_);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int glY = fbH_ - (vpY_ + vpH_);
    glViewport(vpX_, glY, vpW_, vpH_);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX_, glY, vpW_, vpH_);
    glClearColor(c.r, c.g, c.b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    quads_ = 0;
}

void GLESRenderer::draw(const SpriteBatch& batch) {
    if (!ready_ || batch.commands().empty()) return;
    glUseProgram(program_);
    glUniform2f(uVirtual_, virtW_, virtH_);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const auto& verts = batch.vertices();
    for (const DrawCmd& cmd : batch.commands()) {
        glBindTexture(GL_TEXTURE_2D, cmd.texture ? cmd.texture : whiteTex_);
        uint32_t done = 0;
        while (done < cmd.quadCount) {
            uint32_t n = std::min(kMaxQuadsPerDraw, cmd.quadCount - done);
            const Vertex* src = &verts[(size_t)(cmd.firstQuad + done) * 4];
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * 4 * sizeof(Vertex)), src);
            glDrawElements(GL_TRIANGLES, (GLsizei)(n * 6), GL_UNSIGNED_SHORT, nullptr);
            done += n;
            quads_ += n;
        }
    }
}

void GLESRenderer::endFrame() { last_ = quads_; }

}  // namespace aether
#endif
