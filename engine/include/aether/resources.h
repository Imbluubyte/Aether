// Resource manager: caches textures decoded from the game's asset file system.
#pragma once
#include <string>
#include <unordered_map>

#include "aether/filesystem.h"
#include "aether/renderer.h"

namespace aether {

struct TextureInfo {
    TextureId id = 0;
    int w = 0, h = 0;
    bool failed = false;
};

class ResourceManager {
public:
    void configure(IFileSystem* fs, Renderer* r, bool smooth) { fs_ = fs; renderer_ = r; smooth_ = smooth; }
    // Loads (or returns the cached) texture. On failure returns an entry with failed = true (logged once).
    const TextureInfo& texture(const std::string& path);
    bool has(const std::string& path) const { return textures_.find(path) != textures_.end(); }
    // Frees all GPU textures (used on restart / context loss).
    void clear();
    // Drop cache entries without touching the renderer (its GL context is already gone).
    void forget() { textures_.clear(); }
    size_t textureCount() const { return textures_.size(); }

private:
    IFileSystem* fs_ = nullptr;
    Renderer* renderer_ = nullptr;
    bool smooth_ = false;
    std::unordered_map<std::string, TextureInfo> textures_;
};

}  // namespace aether
