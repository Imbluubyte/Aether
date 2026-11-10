// Action-based input. Games bind named actions ("jump", "move_left") to keys, gamepad buttons/axes
// and on-screen virtual controls; scripts only ever query actions, which keeps games portable
// between touch tablets, keyboards and the Switch.
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

#include "aether/platform.h"
#include "aether/types.h"

namespace aether {

struct Touch {
    int id = 0;
    Vec2 pos;          // physical pixels
    bool down = false;
    bool pressedThisFrame = false;
    bool releasedThisFrame = false;
};

struct VirtualStick {
    std::string action;     // drives <action>_left/_right/_up/_down
    std::string anchor;     // bottom_left, bottom_right, top_left, top_right, center
    float dx = 0, dy = 0;   // offset from anchor in virtual pixels
    float radius = 100;
    int touchId = -1;
    Vec2 knob;              // -1..1
};

struct VirtualButton {
    std::string action;
    std::string label;
    std::string anchor;
    float dx = 0, dy = 0;
    float radius = 60;
    int touchId = -1;
};

class Input {
public:
    Input();

    // Binding strings: "key:space", "pad:south", "pad:dpad_left", "axis:leftx-", "axis:rt+".
    bool bind(const std::string& action, const std::string& binding, std::string* err = nullptr);
    void clearBindings(const std::string& action);
    void clearAllBindings();

    void process(const std::vector<Event>& events);
    // Call once per frame after process(): derives pressed/released edges.
    void update(int viewW, int viewH, float virtW, float virtH);

    bool isDown(const std::string& action) const;
    bool pressed(const std::string& action) const;
    bool released(const std::string& action) const;
    float strength(const std::string& action) const;
    Vec2 vector(const std::string& prefix) const;  // <prefix>_left/_right/_up/_down

    // Test/automation hook: force an action's strength (sticky until released with 0).
    void inject(const std::string& action, float strength);
    void injectTouch(int id, float x, float y, int phase /*0 down,1 move,2 up*/);

    const std::vector<Touch>& touches() const { return touches_; }
    bool anyTouchSeen() const { return touchSeen_; }

    std::vector<VirtualStick> sticks;
    std::vector<VirtualButton> buttons;
    // Maps physical pixel coordinates to virtual game coordinates. Set by the engine each frame.
    Vec2 physicalToVirtual(Vec2 p) const;
    void setViewMapping(float offX, float offY, float scale) { offX_ = offX; offY_ = offY; scale_ = scale; }
    // Resolved virtual-control centre in virtual pixels.
    Vec2 anchorPos(const std::string& anchor, float dx, float dy, float virtW, float virtH) const;

    // Emitted for scripts that want raw presses (e.g. back button).
    bool keyDown(Key k) const { return keys_[(size_t)k]; }
    bool keyPressed(Key k) const { return keyEdge_[(size_t)k]; }

private:
    struct Binding { enum Kind { KeyB, PadBtn, AxisPos, AxisNeg } kind; int code; };
    struct Action {
        std::vector<Binding> bindings;
        float strength = 0, injected = 0, virt = 0;
        bool down = false, prevDown = false;
    };
    Action& get(const std::string& name);
    const Action* find(const std::string& name) const;

    std::map<std::string, Action> actions_;
    std::array<bool, (size_t)Key::Count> keys_{}, keyEdge_{};
    std::array<bool, (size_t)PadButton::Count> pad_{};
    std::array<float, (size_t)PadAxis::Count> axes_{};
    std::vector<Touch> touches_;
    bool touchSeen_ = false;
    float offX_ = 0, offY_ = 0, scale_ = 1;
};

}  // namespace aether
