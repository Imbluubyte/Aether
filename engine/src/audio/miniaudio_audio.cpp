#ifdef AETHER_AUDIO_MINIAUDIO
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "aether/audio.h"
#include "aether/log.h"
#include "miniaudio.h"

namespace aether {

namespace {

struct Voice {
    ma_decoder decoder;
    ma_sound sound;
    uint32_t id = 0;
    bool music = false;
    std::shared_ptr<std::vector<uint8_t>> data;
    bool decoderInit = false, soundInit = false;
    ~Voice() {
        if (soundInit) ma_sound_uninit(&sound);
        if (decoderInit) ma_decoder_uninit(&decoder);
    }
};

class MiniaudioAudio : public IAudio {
public:
    explicit MiniaudioAudio(bool noDevice) : noDevice_(noDevice) {}
    ~MiniaudioAudio() override { shutdown(); }

    const char* name() const override { return "miniaudio"; }

    bool init(IFileSystem* assets) override {
        assets_ = assets;
        ma_engine_config cfg = ma_engine_config_init();
        cfg.noDevice = noDevice_ ? MA_TRUE : MA_FALSE;
        if (noDevice_) { cfg.channels = 2; cfg.sampleRate = 48000; }
        if (ma_engine_init(&cfg, &engine_) != MA_SUCCESS) {
            log::warnf("audio: could not open an audio device; running silent");
            return false;
        }
        engineInit_ = true;
        if (ma_sound_group_init(&engine_, 0, nullptr, &sfxGroup_) == MA_SUCCESS) sfxInit_ = true;
        if (ma_sound_group_init(&engine_, 0, nullptr, &musicGroup_) == MA_SUCCESS) musicInit_ = true;
        return true;
    }

    void shutdown() override {
        voices_.clear();
        music_.reset();
        if (sfxInit_) { ma_sound_group_uninit(&sfxGroup_); sfxInit_ = false; }
        if (musicInit_) { ma_sound_group_uninit(&musicGroup_); musicInit_ = false; }
        if (engineInit_) { ma_engine_uninit(&engine_); engineInit_ = false; }
        cache_.clear();
    }

    uint32_t playSound(const std::string& path, float volume, float pitch, bool loop) override {
        if (!engineInit_) return 0;
        if (voices_.size() >= 48) return 0;  // voice cap
        auto v = makeVoice(path, false, volume, loop);
        if (!v) return 0;
        ma_sound_set_pitch(&v->sound, pitch > 0 ? pitch : 1.f);
        ma_sound_start(&v->sound);
        v->id = ++nextId_;
        uint32_t id = v->id;
        voices_.push_back(std::move(v));
        return id;
    }

    void stopSound(uint32_t handle) override {
        for (auto it = voices_.begin(); it != voices_.end(); ++it)
            if ((*it)->id == handle) { voices_.erase(it); return; }
    }

    bool playMusic(const std::string& path, float volume, bool loop) override {
        if (!engineInit_) return false;
        music_.reset();
        auto v = makeVoice(path, true, volume, loop);
        if (!v) return false;
        ma_sound_start(&v->sound);
        music_ = std::move(v);
        return true;
    }

    void stopMusic() override { music_.reset(); }

    void pauseAll(bool paused) override {
        if (!engineInit_) return;
        if (paused) ma_engine_stop(&engine_); else ma_engine_start(&engine_);
    }

    void setVolume(const std::string& bus, float v) override {
        v = std::max(0.f, std::min(1.f, v));
        if (bus == "master") { master_ = v; if (engineInit_) ma_engine_set_volume(&engine_, v); }
        else if (bus == "sfx") { sfx_ = v; if (sfxInit_) ma_sound_group_set_volume(&sfxGroup_, v); }
        else { musicVol_ = v; if (musicInit_) ma_sound_group_set_volume(&musicGroup_, v); }
    }

    float volume(const std::string& bus) const override { return bus == "master" ? master_ : (bus == "sfx" ? sfx_ : musicVol_); }

    void update() override {
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const std::unique_ptr<Voice>& v) { return ma_sound_at_end(&v->sound) == MA_TRUE; }),
                      voices_.end());
    }

    int activeSounds() const override { return (int)voices_.size(); }

private:
    std::shared_ptr<std::vector<uint8_t>> load(const std::string& path) {
        auto it = cache_.find(path);
        if (it != cache_.end()) return it->second;
        auto data = std::make_shared<std::vector<uint8_t>>();
        if (!assets_ || !assets_->readFile(path, *data)) return nullptr;
        // Only cache small clips; music streams are re-read so large files don't pin memory.
        if (data->size() < (1u << 20)) cache_[path] = data;
        return data;
    }

    std::unique_ptr<Voice> makeVoice(const std::string& path, bool music, float volume, bool loop) {
        auto data = load(path);
        if (!data || data->empty()) { log::warnf("audio: cannot read '%s'", path.c_str()); return nullptr; }
        auto v = std::make_unique<Voice>();
        v->data = data;
        v->music = music;
        if (ma_decoder_init_memory(data->data(), data->size(), nullptr, &v->decoder) != MA_SUCCESS) {
            log::warnf("audio: unsupported format '%s' (use wav, mp3 or flac)", path.c_str());
            return nullptr;
        }
        v->decoderInit = true;
        ma_sound_group* group = music ? (musicInit_ ? &musicGroup_ : nullptr) : (sfxInit_ ? &sfxGroup_ : nullptr);
        if (ma_sound_init_from_data_source(&engine_, &v->decoder, 0, group, &v->sound) != MA_SUCCESS) return nullptr;
        v->soundInit = true;
        ma_sound_set_looping(&v->sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_volume(&v->sound, std::max(0.f, volume));
        return v;
    }

    bool noDevice_;
    IFileSystem* assets_ = nullptr;
    ma_engine engine_;
    ma_sound_group sfxGroup_, musicGroup_;
    bool engineInit_ = false, sfxInit_ = false, musicInit_ = false;
    std::vector<std::unique_ptr<Voice>> voices_;
    std::unique_ptr<Voice> music_;
    std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> cache_;
    uint32_t nextId_ = 0;
    float master_ = 1, sfx_ = 1, musicVol_ = 1;
};

}  // namespace

IAudio* createMiniaudioAudio(bool noDevice) { return new MiniaudioAudio(noDevice); }

}  // namespace aether
#endif
