// Built-in 5x7 bitmap font so every game can show scores/HUD text with zero assets.
#pragma once
#include <string>
#include <vector>

#include "aether/renderer.h"

namespace aether {

class Font {
public:
    static constexpr int kCellW = 6;  // 5 px glyph + 1 px spacing
    static constexpr int kCellH = 8;  // 7 px glyph + 1 px spacing
    static constexpr int kCols = 16;
    static constexpr int kRows = 6;   // ASCII 32..127

    // Builds the RGBA atlas (white glyphs with alpha) the renderer should upload.
    static void buildAtlas(std::vector<uint8_t>& rgba, int& w, int& h);
    void setTexture(TextureId t) { tex_ = t; }
    TextureId texture() const { return tex_; }

    // `size` = pixel multiplier (size 2 draws glyphs 10x14 px). align: 0 left, 1 centre, 2 right.
    void draw(SpriteBatch& batch, const std::string& text, float x, float y, float size, Color c, int align) const;
    Vec2 measure(const std::string& text, float size) const;

private:
    TextureId tex_ = 0;
};

}  // namespace aether
