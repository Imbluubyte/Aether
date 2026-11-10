// Headless platform: fixed-size framebuffer, deterministic clock and a scriptable event queue.
// Used by the test runner, CI and `aether_run`.
#pragma once
#include <vector>

#include "aether/platform.h"

namespace aether {

class HeadlessPlatform : public IPlatform {
public:
    HeadlessPlatform(int w, int h) : w_(w), h_(h) {}
    const char* name() const override { return "headless"; }
    void pollEvents(std::vector<Event>& out) override {
        out.insert(out.end(), queue_.begin(), queue_.end());
        queue_.clear();
    }
    void present() override { frames++; }
    int width() const override { return w_; }
    int height() const override { return h_; }
    double timeSeconds() const override { return clock_; }
    bool hasTouchscreen() const override { return touch_; }

    void push(const Event& e) { queue_.push_back(e); }
    void setSize(int w, int h) { w_ = w; h_ = h; }
    void advance(double dt) { clock_ += dt; }
    void setTouch(bool t) { touch_ = t; }
    int frames = 0;

private:
    int w_, h_;
    double clock_ = 0;
    bool touch_ = false;
    std::vector<Event> queue_;
};

}  // namespace aether
