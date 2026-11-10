// Android backend pieces shared by the Kotlin editor (JNI) and the standalone NativeActivity player.
// Nothing here depends on Kotlin/Java types.
#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "aether/filesystem.h"
#include "aether/platform.h"

struct AAssetManager;

namespace aether {

class AndroidPlatform : public IPlatform {
public:
    AndroidPlatform();
    const char* name() const override { return "android"; }
    void pollEvents(std::vector<Event>& out) override;
    void present() override { if (presentFn) presentFn(); }
    int width() const override { return w_; }
    int height() const override { return h_; }
    double timeSeconds() const override;
    bool hasTouchscreen() const override { return true; }

    // Thread-safe: may be called from the UI / input threads.
    void pushEvent(const Event& e);
    void setSize(int w, int h) { w_ = w; h_ = h; }

    // Called by present(); the NativeActivity player sets this to eglSwapBuffers (GLSurfaceView swaps itself).
    std::function<void()> presentFn;

    // android/keycodes.h value -> engine key / gamepad button. Returns false if unmapped.
    static bool keyFromAndroid(int keycode, Key& out);
    static bool padButtonFromAndroid(int keycode, PadButton& out);
    // Gamepad axis ids as used by Android MotionEvent (AXIS_X=0, AXIS_Y=1, AXIS_Z=11, AXIS_RZ=14,
    // AXIS_LTRIGGER=17, AXIS_RTRIGGER=18, AXIS_HAT_X=15, AXIS_HAT_Y=16).
    void pushAxis(int androidAxis, float value);
    void pushKey(bool down, int keycode);
    // action: 0 down, 1 move, 2 up
    void pushTouch(int action, int id, float x, float y);

private:
    std::mutex mutex_;
    std::vector<Event> queue_;
    int w_ = 1, h_ = 1;
    std::chrono::steady_clock::time_point start_;
    float hatX_ = 0, hatY_ = 0;
};

// Reads game files from the APK's assets/<prefix>/ directory (standalone player).
class AAssetFileSystem : public IFileSystem {
public:
    AAssetFileSystem(AAssetManager* mgr, std::string prefix) : mgr_(mgr), prefix_(std::move(prefix)) {}
    bool readFile(const std::string& path, std::vector<uint8_t>& out) override;
    bool exists(const std::string& path) override;

private:
    AAssetManager* mgr_;
    std::string prefix_;
};

}  // namespace aether
