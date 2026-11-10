// Audio interface. Backends: NullAudio (tests/headless), MiniaudioAudio (Android + desktop).
#pragma once
#include <cstdint>
#include <string>

#include "aether/filesystem.h"

namespace aether {

class IAudio {
public:
    virtual ~IAudio() = default;
    virtual const char* name() const = 0;
    virtual bool init(IFileSystem* assets) = 0;
    virtual void shutdown() = 0;
    // Returns a handle (>0) or 0 on failure (missing/unsupported file).
    virtual uint32_t playSound(const std::string& path, float volume, float pitch, bool loop) = 0;
    virtual void stopSound(uint32_t handle) = 0;
    virtual bool playMusic(const std::string& path, float volume, bool loop) = 0;
    virtual void stopMusic() = 0;
    virtual void pauseAll(bool paused) = 0;
    // bus: "master", "sfx" or "music". Volume 0..1.
    virtual void setVolume(const std::string& bus, float volume) = 0;
    virtual float volume(const std::string& bus) const = 0;
    virtual void update() = 0;  // once per frame: reclaim finished sounds
    virtual int activeSounds() const = 0;
};

// Records requests but produces no sound. Lets scripts be tested without an audio device.
class NullAudio : public IAudio {
public:
    const char* name() const override { return "null"; }
    bool init(IFileSystem* assets) override { assets_ = assets; return true; }
    void shutdown() override {}
    uint32_t playSound(const std::string& path, float, float, bool) override {
        if (assets_ && !assets_->exists(path)) return 0;
        played++;
        return ++next_;
    }
    void stopSound(uint32_t) override {}
    bool playMusic(const std::string& path, float, bool) override {
        if (assets_ && !assets_->exists(path)) return false;
        musicPlaying = true;
        return true;
    }
    void stopMusic() override { musicPlaying = false; }
    void pauseAll(bool) override {}
    void setVolume(const std::string& bus, float v) override { if (bus == "master") master_ = v; else if (bus == "sfx") sfx_ = v; else music_ = v; }
    float volume(const std::string& bus) const override { return bus == "master" ? master_ : (bus == "sfx" ? sfx_ : music_); }
    void update() override {}
    int activeSounds() const override { return 0; }
    int played = 0;
    bool musicPlaying = false;

private:
    IFileSystem* assets_ = nullptr;
    uint32_t next_ = 0;
    float master_ = 1, sfx_ = 1, music_ = 1;
};

#ifdef AETHER_AUDIO_MINIAUDIO
IAudio* createMiniaudioAudio(bool noDevice);
#endif

}  // namespace aether
