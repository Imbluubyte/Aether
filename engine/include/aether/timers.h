// Script timers. Each timer holds an opaque integer reference (a Lua registry ref owned by the VM).
#pragma once
#include <vector>

#include "aether/types.h"

namespace aether {

struct Timer {
    int id = 0;
    float remaining = 0;
    float interval = 0;   // 0 = one-shot
    int repeatsLeft = -1; // -1 = forever (only for interval timers)
    int ref = 0;
    NodeId owner = kNoNode;
    bool dead = false;
};

class Timers {
public:
    int add(float delay, float interval, int repeat, int ref, NodeId owner);
    bool cancel(int id);
    bool active(int id) const;
    void cancelOwner(NodeId owner);
    void clear();
    size_t count() const { return timers_.size(); }

    // Advances time and invokes fire(timer) for each due timer. fire returns false to cancel an
    // interval timer. Newly added timers do not fire until the next update.
    template <typename F>
    void update(float dt, F&& fire) {
        size_t n = timers_.size();
        for (size_t i = 0; i < n && i < timers_.size(); i++) {
            if (timers_[i].dead) continue;
            timers_[i].remaining -= dt;
            int guard = 0;
            while (!timers_[i].dead && timers_[i].remaining <= 0 && guard++ < 8) {
                Timer snapshot = timers_[i];
                bool keep = fire(snapshot);
                Timer& t = timers_[i];
                if (snapshot.interval <= 0 || !keep) { t.dead = true; break; }
                if (t.repeatsLeft > 0 && --t.repeatsLeft == 0) { t.dead = true; break; }
                t.remaining += snapshot.interval;
            }
        }
        // Compact dead timers, handing their refs to the release list.
        size_t w = 0;
        for (size_t i = 0; i < timers_.size(); i++) {
            if (timers_[i].dead) released_.push_back(timers_[i].ref);
            else timers_[w++] = timers_[i];
        }
        timers_.resize(w);
    }

    // Refs of timers that finished or were cancelled; the VM must luaL_unref each one.
    std::vector<int> takeReleased() {
        std::vector<int> r;
        r.swap(released_);
        return r;
    }

private:
    std::vector<Timer> timers_;
    std::vector<int> released_;
    int nextId_ = 1;
};

}  // namespace aether
