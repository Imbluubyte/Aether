#include "aether/resources.h"

#include "aether/log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"

namespace aether {

const TextureInfo& ResourceManager::texture(const std::string& path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second;
    TextureInfo info;
    std::vector<uint8_t> bytes;
    if (!fs_ || !fs_->readFile(path, bytes)) {
        log::errorf("texture not found: '%s'", path.c_str());
        info.failed = true;
    } else {
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
        if (!px) {
            log::errorf("cannot decode texture '%s' (png, jpg, bmp and tga are supported)", path.c_str());
            info.failed = true;
        } else {
            info.id = renderer_ ? renderer_->createTexture(w, h, px, smooth_) : 0;
            info.w = w;
            info.h = h;
            stbi_image_free(px);
        }
    }
    return textures_.emplace(path, info).first->second;
}

void ResourceManager::clear() {
    if (renderer_)
        for (auto& kv : textures_)
            if (kv.second.id) renderer_->destroyTexture(kv.second.id);
    textures_.clear();
}

}  // namespace aether
