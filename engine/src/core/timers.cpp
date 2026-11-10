#include "aether/timers.h"

namespace aether {

int Timers::add(float delay, float interval, int repeat, int ref, NodeId owner) {
    Timer t;
    t.id = nextId_++;
    t.remaining = delay < 0 ? 0 : delay;
    t.interval = interval;
    t.repeatsLeft = repeat;
    t.ref = ref;
    t.owner = owner;
    timers_.push_back(t);
    return t.id;
}

bool Timers::cancel(int id) {
    for (auto& t : timers_)
        if (t.id == id && !t.dead) { t.dead = true; return true; }
    return false;
}

bool Timers::active(int id) const {
    for (auto& t : timers_)
        if (t.id == id && !t.dead) return true;
    return false;
}

void Timers::cancelOwner(NodeId owner) {
    for (auto& t : timers_)
        if (t.owner == owner) t.dead = true;
}

void Timers::clear() {
    for (auto& t : timers_) released_.push_back(t.ref);
    timers_.clear();
}

}  // namespace aether
