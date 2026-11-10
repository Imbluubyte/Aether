// Platform abstraction. Everything OS-specific (window/GL context, raw input, clock) lives behind
// IPlatform so the engine core stays portable to Android, desktop test hosts and Nintendo Switch.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aether {

enum class Key : uint16_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Left, Right, Up, Down,
    Space, Enter, Escape, Tab, Backspace, Shift, Ctrl, Alt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

// Gamepad buttons named by physical position so Switch (A/B swapped) and Xbox layouts agree.
enum class PadButton : uint8_t {
    South, East, West, North,   // Xbox A,B,X,Y   Switch B,A,Y,X
    LeftShoulder, RightShoulder,
    Back, Start,
    LeftStick, RightStick,
    DpadUp, DpadDown, DpadLeft, DpadRight,
    Count
};

enum class PadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

struct Event {
    enum Type : uint8_t {
        KeyDown, KeyUp,
        PadButtonDown, PadButtonUp, PadAxisMoved,
        TouchDown, TouchMove, TouchUp,   // x,y in physical pixels
        Resize,                          // x,y = new framebuffer size
        Pause, Resume, Quit
    };
    Type type = Quit;
    int code = 0;      // Key / PadButton / PadAxis / touch id
    float x = 0, y = 0;
    float value = 0;   // axis value
};

bool keyFromName(const std::string& name, Key& out);
bool padButtonFromName(const std::string& name, PadButton& out);
bool padAxisFromName(const std::string& name, PadAxis& out);

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual const char* name() const = 0;
    virtual void pollEvents(std::vector<Event>& out) = 0;
    // Present the rendered frame (swap buffers). May block for vsync.
    virtual void present() = 0;
    virtual int width() const = 0;   // framebuffer size in physical pixels
    virtual int height() const = 0;
    virtual double timeSeconds() const = 0;
    virtual bool hasTouchscreen() const { return false; }
    virtual bool shouldQuit() const { return false; }
};

}  // namespace aether
