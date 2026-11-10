#include "aether/input.h"

#include <algorithm>
#include <cmath>

namespace aether {

static constexpr float kDeadzone = 0.2f;
static constexpr float kDownThreshold = 0.5f;

Input::Input() = default;

Input::Action& Input::get(const std::string& name) { return actions_[name]; }

const Input::Action* Input::find(const std::string& name) const {
    auto it = actions_.find(name);
    return it == actions_.end() ? nullptr : &it->second;
}

bool Input::bind(const std::string& action, const std::string& binding, std::string* err) {
    auto colon = binding.find(':');
    if (colon == std::string::npos) {
        if (err) *err = "binding must look like 'key:space', 'pad:south' or 'axis:leftx-' (got '" + binding + "')";
        return false;
    }
    std::string kind = binding.substr(0, colon), name = binding.substr(colon + 1);
    Binding b{};
    if (kind == "key") {
        Key k;
        if (!keyFromName(name, k)) { if (err) *err = "unknown key '" + name + "'"; return false; }
        b = {Binding::KeyB, (int)k};
    } else if (kind == "pad") {
        PadButton pb;
        if (!padButtonFromName(name, pb)) { if (err) *err = "unknown gamepad button '" + name + "'"; return false; }
        b = {Binding::PadBtn, (int)pb};
    } else if (kind == "axis") {
        if (name.empty() || (name.back() != '+' && name.back() != '-')) {
            if (err) *err = "axis binding needs a trailing + or - (e.g. 'axis:leftx-')";
            return false;
        }
        bool pos = name.back() == '+';
        name.pop_back();
        PadAxis ax;
        if (!padAxisFromName(name, ax)) { if (err) *err = "unknown axis '" + name + "'"; return false; }
        b = {pos ? Binding::AxisPos : Binding::AxisNeg, (int)ax};
    } else {
        if (err) *err = "unknown binding kind '" + kind + "' (use key, pad or axis)";
        return false;
    }
    get(action).bindings.push_back(b);
    return true;
}

void Input::clearBindings(const std::string& action) {
    auto it = actions_.find(action);
    if (it != actions_.end()) it->second.bindings.clear();
}
void Input::clearAllBindings() {
    for (auto& kv : actions_) kv.second.bindings.clear();
}

void Input::process(const std::vector<Event>& events) {
    // Edge flags last exactly one frame: clear, then apply this frame's events.
    keyEdge_.fill(false);
    touches_.erase(std::remove_if(touches_.begin(), touches_.end(), [](const Touch& t) { return !t.down; }), touches_.end());
    for (auto& t : touches_) { t.pressedThisFrame = false; t.releasedThisFrame = false; }

    auto touchFor = [&](int id) -> Touch* {
        for (auto& t : touches_)
            if (t.id == id) return &t;
        return nullptr;
    };
    for (const Event& e : events) {
        switch (e.type) {
            case Event::KeyDown:
                if (e.code > 0 && e.code < (int)Key::Count) {
                    if (!keys_[(size_t)e.code]) keyEdge_[(size_t)e.code] = true;
                    keys_[(size_t)e.code] = true;
                }
                break;
            case Event::KeyUp:
                if (e.code > 0 && e.code < (int)Key::Count) keys_[(size_t)e.code] = false;
                break;
            case Event::PadButtonDown:
                if (e.code >= 0 && e.code < (int)PadButton::Count) pad_[(size_t)e.code] = true;
                break;
            case Event::PadButtonUp:
                if (e.code >= 0 && e.code < (int)PadButton::Count) pad_[(size_t)e.code] = false;
                break;
            case Event::PadAxisMoved:
                if (e.code >= 0 && e.code < (int)PadAxis::Count) axes_[(size_t)e.code] = e.value;
                break;
            case Event::TouchDown: {
                touchSeen_ = true;
                Touch* t = touchFor(e.code);
                if (!t) { touches_.push_back({}); t = &touches_.back(); }
                t->id = e.code; t->pos = {e.x, e.y}; t->down = true; t->pressedThisFrame = true;
                break;
            }
            case Event::TouchMove:
                if (Touch* t = touchFor(e.code)) t->pos = {e.x, e.y};
                break;
            case Event::TouchUp:
                if (Touch* t = touchFor(e.code)) { t->pos = {e.x, e.y}; t->down = false; t->releasedThisFrame = true; }
                break;
            default: break;
        }
    }
}

void Input::injectTouch(int id, float x, float y, int phase) {
    Event e;
    e.type = phase == 0 ? Event::TouchDown : (phase == 1 ? Event::TouchMove : Event::TouchUp);
    e.code = id; e.x = x; e.y = y;
    // Applied immediately but keep edge flags intact for this frame.
    Touch* t = nullptr;
    for (auto& tt : touches_) if (tt.id == id) t = &tt;
    if (phase == 0) {
        touchSeen_ = true;
        if (!t) { touches_.push_back({}); t = &touches_.back(); }
        t->id = id; t->pos = {x, y}; t->down = true; t->pressedThisFrame = true;
    } else if (t) {
        t->pos = {x, y};
        if (phase == 2) { t->down = false; t->releasedThisFrame = true; }
    }
}

Vec2 Input::physicalToVirtual(Vec2 p) const { return {(p.x - offX_) / scale_, (p.y - offY_) / scale_}; }

Vec2 Input::anchorPos(const std::string& anchor, float dx, float dy, float vw, float vh) const {
    float bx = 0, by = 0;
    if (anchor == "bottom_left") { bx = 0; by = vh; }
    else if (anchor == "bottom_right") { bx = vw; by = vh; }
    else if (anchor == "top_right") { bx = vw; by = 0; }
    else if (anchor == "center") { bx = vw * 0.5f; by = vh * 0.5f; }
    return {bx + dx, by + dy};
}

void Input::update(int /*viewW*/, int /*viewH*/, float virtW, float virtH) {
    for (auto& kv : actions_) kv.second.virt = 0;

    // Virtual controls claim touches.
    auto claimed = [&](int id) {
        for (auto& s : sticks) if (s.touchId == id) return true;
        for (auto& b : buttons) if (b.touchId == id) return true;
        return false;
    };
    auto touchById = [&](int id) -> const Touch* {
        for (auto& t : touches_) if (t.id == id && t.down) return &t;
        return nullptr;
    };
    for (auto& s : sticks) {
        Vec2 c = anchorPos(s.anchor, s.dx, s.dy, virtW, virtH);
        if (s.touchId >= 0 && !touchById(s.touchId)) { s.touchId = -1; s.knob = {}; }
        if (s.touchId < 0) {
            for (auto& t : touches_) {
                if (!t.down || !t.pressedThisFrame || claimed(t.id)) continue;
                Vec2 p = physicalToVirtual(t.pos);
                if ((p - c).length() <= s.radius * 1.5f) { s.touchId = t.id; break; }
            }
        }
        if (s.touchId >= 0) {
            const Touch* t = touchById(s.touchId);
            Vec2 d = physicalToVirtual(t->pos) - c;
            d = d * (1.f / s.radius);
            float len = d.length();
            if (len > 1.f) d = d * (1.f / len);
            s.knob = d;
            auto setv = [&](const char* suffix, float v) {
                if (v > 0) { Action& a = get(s.action + suffix); a.virt = std::max(a.virt, v); }
            };
            setv("_left", -d.x); setv("_right", d.x); setv("_up", -d.y); setv("_down", d.y);
        }
    }
    for (auto& b : buttons) {
        Vec2 c = anchorPos(b.anchor, b.dx, b.dy, virtW, virtH);
        if (b.touchId >= 0 && !touchById(b.touchId)) b.touchId = -1;
        if (b.touchId < 0) {
            for (auto& t : touches_) {
                if (!t.down || !t.pressedThisFrame || claimed(t.id)) continue;
                if ((physicalToVirtual(t.pos) - c).length() <= b.radius) { b.touchId = t.id; break; }
            }
        }
        if (b.touchId >= 0) get(b.action).virt = 1;
    }

    for (auto& kv : actions_) {
        Action& a = kv.second;
        float s = 0;
        for (const Binding& b : a.bindings) {
            float v = 0;
            switch (b.kind) {
                case Binding::KeyB: v = keys_[(size_t)b.code] ? 1.f : 0.f; break;
                case Binding::PadBtn: v = pad_[(size_t)b.code] ? 1.f : 0.f; break;
                case Binding::AxisPos: v = std::max(0.f, axes_[(size_t)b.code]); break;
                case Binding::AxisNeg: v = std::max(0.f, -axes_[(size_t)b.code]); break;
            }
            if ((b.kind == Binding::AxisPos || b.kind == Binding::AxisNeg)) v = v < kDeadzone ? 0.f : (v - kDeadzone) / (1.f - kDeadzone);
            s = std::max(s, v);
        }
        s = std::max(s, std::max(a.injected, a.virt));
        a.strength = s;
        a.prevDown = a.down;
        a.down = s > kDownThreshold;
    }
}

bool Input::isDown(const std::string& n) const { auto a = find(n); return a && a->down; }
bool Input::pressed(const std::string& n) const { auto a = find(n); return a && a->down && !a->prevDown; }
bool Input::released(const std::string& n) const { auto a = find(n); return a && !a->down && a->prevDown; }
float Input::strength(const std::string& n) const { auto a = find(n); return a ? a->strength : 0.f; }

Vec2 Input::vector(const std::string& p) const {
    Vec2 v{strength(p + "_right") - strength(p + "_left"), strength(p + "_down") - strength(p + "_up")};
    float len = v.length();
    if (len > 1.f) v = v * (1.f / len);
    return v;
}

void Input::inject(const std::string& action, float s) { get(action).injected = s; }

}  // namespace aether
