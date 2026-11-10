#include "android_platform.h"

#include <android/asset_manager.h>

namespace aether {

AndroidPlatform::AndroidPlatform() : start_(std::chrono::steady_clock::now()) {}

double AndroidPlatform::timeSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

void AndroidPlatform::pollEvents(std::vector<Event>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.insert(out.end(), queue_.begin(), queue_.end());
    queue_.clear();
}

void AndroidPlatform::pushEvent(const Event& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() < 4096) queue_.push_back(e);  // never grow without bound if the loop stalls
}

bool AndroidPlatform::keyFromAndroid(int k, Key& out) {
    if (k >= 29 && k <= 54) { out = (Key)((int)Key::A + (k - 29)); return true; }       // AKEYCODE_A..Z
    if (k >= 7 && k <= 16) { out = (Key)((int)Key::Num0 + (k - 7)); return true; }      // AKEYCODE_0..9
    if (k >= 131 && k <= 142) { out = (Key)((int)Key::F1 + (k - 131)); return true; }   // AKEYCODE_F1..F12
    switch (k) {
        case 21: out = Key::Left; return true;
        case 22: out = Key::Right; return true;
        case 19: out = Key::Up; return true;
        case 20: out = Key::Down; return true;
        case 62: out = Key::Space; return true;
        case 66: case 160: out = Key::Enter; return true;
        case 111: case 4: out = Key::Escape; return true;  // 4 = AKEYCODE_BACK
        case 61: out = Key::Tab; return true;
        case 67: out = Key::Backspace; return true;
        case 59: case 60: out = Key::Shift; return true;
        case 113: case 114: out = Key::Ctrl; return true;
        case 57: case 58: out = Key::Alt; return true;
        default: return false;
    }
}

bool AndroidPlatform::padButtonFromAndroid(int k, PadButton& out) {
    switch (k) {
        case 96: out = PadButton::South; return true;          // BUTTON_A
        case 97: out = PadButton::East; return true;           // BUTTON_B
        case 99: out = PadButton::West; return true;           // BUTTON_X
        case 100: out = PadButton::North; return true;         // BUTTON_Y
        case 102: out = PadButton::LeftShoulder; return true;  // BUTTON_L1
        case 103: out = PadButton::RightShoulder; return true; // BUTTON_R1
        case 109: out = PadButton::Back; return true;          // BUTTON_SELECT
        case 108: out = PadButton::Start; return true;         // BUTTON_START
        case 106: out = PadButton::LeftStick; return true;     // BUTTON_THUMBL
        case 107: out = PadButton::RightStick; return true;    // BUTTON_THUMBR
        default: return false;
    }
}

void AndroidPlatform::pushKey(bool down, int keycode) {
    Event e;
    PadButton pb;
    if (padButtonFromAndroid(keycode, pb)) {
        e.type = down ? Event::PadButtonDown : Event::PadButtonUp;
        e.code = (int)pb;
        pushEvent(e);
        return;
    }
    Key k;
    if (!keyFromAndroid(keycode, k)) return;
    e.type = down ? Event::KeyDown : Event::KeyUp;
    e.code = (int)k;
    pushEvent(e);
    // D-pad keys from a gamepad also drive the pad d-pad buttons so one binding set works for both.
    PadButton dp;
    bool isDpad = true;
    if (k == Key::Left) dp = PadButton::DpadLeft;
    else if (k == Key::Right) dp = PadButton::DpadRight;
    else if (k == Key::Up) dp = PadButton::DpadUp;
    else if (k == Key::Down) dp = PadButton::DpadDown;
    else isDpad = false;
    if (isDpad) {
        Event p;
        p.type = down ? Event::PadButtonDown : Event::PadButtonUp;
        p.code = (int)dp;
        pushEvent(p);
    }
}

void AndroidPlatform::pushAxis(int axis, float v) {
    Event e;
    e.type = Event::PadAxisMoved;
    e.value = v;
    switch (axis) {
        case 0: e.code = (int)PadAxis::LeftX; break;
        case 1: e.code = (int)PadAxis::LeftY; break;
        case 11: e.code = (int)PadAxis::RightX; break;
        case 14: e.code = (int)PadAxis::RightY; break;
        case 17: e.code = (int)PadAxis::LeftTrigger; break;
        case 18: e.code = (int)PadAxis::RightTrigger; break;
        case 15: case 16: {  // hat -> d-pad buttons
            float& h = axis == 15 ? hatX_ : hatY_;
            float old = h;
            h = v;
            auto edge = [&](bool wasOn, bool isOn, PadButton b) {
                if (wasOn == isOn) return;
                Event p;
                p.type = isOn ? Event::PadButtonDown : Event::PadButtonUp;
                p.code = (int)b;
                pushEvent(p);
            };
            if (axis == 15) { edge(old < -0.5f, h < -0.5f, PadButton::DpadLeft); edge(old > 0.5f, h > 0.5f, PadButton::DpadRight); }
            else { edge(old < -0.5f, h < -0.5f, PadButton::DpadUp); edge(old > 0.5f, h > 0.5f, PadButton::DpadDown); }
            return;
        }
        default: return;
    }
    pushEvent(e);
}

void AndroidPlatform::pushTouch(int action, int id, float x, float y) {
    Event e;
    e.type = action == 0 ? Event::TouchDown : (action == 1 ? Event::TouchMove : Event::TouchUp);
    e.code = id;
    e.x = x;
    e.y = y;
    pushEvent(e);
}

bool AAssetFileSystem::readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::string norm;
    if (!normalizePath(path, norm)) return false;
    AAsset* a = AAssetManager_open(mgr_, (prefix_ + "/" + norm).c_str(), AASSET_MODE_BUFFER);
    if (!a) return false;
    off_t len = AAsset_getLength(a);
    out.resize((size_t)len);
    int n = len > 0 ? AAsset_read(a, out.data(), (size_t)len) : 0;
    AAsset_close(a);
    return n == (int)len;
}

bool AAssetFileSystem::exists(const std::string& path) {
    std::string norm;
    if (!normalizePath(path, norm)) return false;
    AAsset* a = AAssetManager_open(mgr_, (prefix_ + "/" + norm).c_str(), AASSET_MODE_UNKNOWN);
    if (!a) return false;
    AAsset_close(a);
    return true;
}

}  // namespace aether
