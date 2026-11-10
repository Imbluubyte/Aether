// Plain C++ unit tests for the engine core (no framework dependency).
#include <cstdio>
#include <memory>

#include "aether/engine.h"
#include "aether/log.h"
#include "aether/platform_headless.h"

using namespace aether;

static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_failed++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void testPaths() {
    std::string o;
    CHECK(normalizePath("a/b.lua", o) && o == "a/b.lua");
    CHECK(normalizePath("./a//b.lua", o) && o == "a/b.lua");
    CHECK(normalizePath("\\a\\b.lua", o) && o == "a/b.lua");
    CHECK(!normalizePath("../secret", o));
    CHECK(!normalizePath("a/../../x", o));
    CHECK(normalizePath("a/../b", o) && o == "b");
}

static void testStorage() {
    MemoryStorage m;
    CHECK(m.write("a.sav", "x"));
    std::string out;
    CHECK(m.read("a.sav", out) && out == "x");
    CHECK(m.remove("a.sav") && !m.exists("a.sav"));
}

static void testScene() {
    SceneTree t;
    NodeId a = t.create(NodeType::Node, "a"), b = t.create(NodeType::Sprite, "b");
    CHECK(t.valid(a) && t.valid(b));
    CHECK(t.addChild(a, b));
    CHECK(!t.addChild(b, a));  // cycle rejected
    CHECK(t.findChild(t.root(), "b", true) == b);
    t.addToGroup(b, "g");
    CHECK(t.groupCount("g") == 1);
    t.destroy(a);  // destroys subtree
    CHECK(!t.valid(a) && !t.valid(b));
    CHECK(t.groupCount("g") == 0);
    auto freed = t.collectGarbage();
    CHECK(freed.size() == 2);
    NodeId c = t.create(NodeType::Node, "c");  // slot reuse must produce a new generation
    CHECK(c != a && c != b);
    CHECK(!t.valid(a) && t.get(a) == nullptr);
}

static void testInput() {
    Input in;
    std::string err;
    CHECK(in.bind("jump", "key:space", &err));
    CHECK(!in.bind("x", "key:nonsense", &err));
    CHECK(!in.bind("x", "axis:leftx", &err));
    Event e;
    e.type = Event::KeyDown; e.code = (int)Key::Space;
    in.process({e});
    in.update(100, 100, 100, 100);
    CHECK(in.pressed("jump") && in.isDown("jump"));
    in.process({});
    in.update(100, 100, 100, 100);
    CHECK(!in.pressed("jump") && in.isDown("jump"));
    e.type = Event::KeyUp;
    in.process({e});
    in.update(100, 100, 100, 100);
    CHECK(in.released("jump") && !in.isDown("jump"));
}

static void testPhysics() {
    SceneTree t;
    Physics p(t);
    p.layers.set({"a", "b"});
    NodeId wall = t.create(NodeType::Body, "wall");
    { Node* n = t.get(wall); n->w = 100; n->h = 20; n->pos = {0, 50}; n->body = BodyType::Static; n->layer = 1; }
    NodeId box = t.create(NodeType::Body, "box");
    { Node* n = t.get(box); n->w = 10; n->h = 10; n->pos = {0, 0}; n->body = BodyType::Dynamic; n->layer = 2; n->mask = 1; }
    p.gravity = {0, 500};
    int enters = 0;
    for (int i = 0; i < 120; i++) {
        t.updateTransforms();
        p.step(1.f / 60.f, [&](NodeId, NodeId, bool enter) { if (enter) enters++; });
    }
    Node* b = t.get(box);
    CHECK(b->onFloor);
    CHECK(std::fabs(b->pos.y - 35.f) < 0.5f);  // rests on top of the wall (wall top = 40, half box = 5)
    t.updateTransforms();
    p.invalidate();
    CHECK(p.queryCircle({0, 35}, 3, 0xffffffffu).size() >= 1);
    auto hit = p.raycast({0, -100}, {0, 100}, 1);
    CHECK(hit.hit && hit.node == wall && std::fabs(hit.point.y - 40.f) < 0.01f);
    CHECK(enters >= 0);
}

static void testTimers() {
    Timers tm;
    int fired = 0;
    tm.add(0.5f, 0, 0, 1, kNoNode);
    tm.add(0.25f, 0.25f, 3, 2, kNoNode);
    for (int i = 0; i < 100; i++) tm.update(0.05f, [&](const Timer&) { fired++; return true; });
    CHECK(fired == 4);  // one-shot + 3 repeats
    CHECK(tm.count() == 0);
    CHECK(tm.takeReleased().size() == 2);
}

#ifdef AETHER_AUDIO_MINIAUDIO
// In-memory file system holding one generated WAV, to exercise the real miniaudio decode path without a sound card.
struct WavFs : IFileSystem {
    std::vector<uint8_t> wav;
    WavFs() {
        const int n = 800;  // 0.1 s of 8 kHz mono 16-bit
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) wav.push_back((uint8_t)(v >> (8 * i))); };
        auto u16 = [&](uint16_t v) { wav.push_back((uint8_t)v); wav.push_back((uint8_t)(v >> 8)); };
        for (char c : std::string("RIFF")) wav.push_back((uint8_t)c);
        u32(36 + n * 2);
        for (char c : std::string("WAVEfmt ")) wav.push_back((uint8_t)c);
        u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
        for (char c : std::string("data")) wav.push_back((uint8_t)c);
        u32(n * 2);
        for (int i = 0; i < n; i++) u16((uint16_t)(i % 40 < 20 ? 8000 : -8000));
    }
    bool readFile(const std::string& p, std::vector<uint8_t>& out) override { if (p != "a.wav") return false; out = wav; return true; }
    bool exists(const std::string& p) override { return p == "a.wav"; }
};

static void testMiniaudio() {
    WavFs fs;
    std::unique_ptr<IAudio> a(createMiniaudioAudio(true));
    CHECK(a->init(&fs));
    uint32_t h = a->playSound("a.wav", 1.f, 1.f, false);
    CHECK(h != 0 && a->activeSounds() == 1);
    CHECK(a->playSound("missing.wav", 1.f, 1.f, false) == 0);
    a->setVolume("sfx", 0.5f);
    CHECK(a->volume("sfx") == 0.5f);
    a->stopSound(h);
    CHECK(a->activeSounds() == 0);
    a->shutdown();
}
#endif

static std::unique_ptr<Engine> makeEngine(const char* dir) {
    Services s;
    s.platform = std::make_unique<HeadlessPlatform>(640, 360);
    s.assets = std::make_unique<DirFileSystem>(dir);
    s.storage = std::make_unique<MemoryStorage>();
    s.renderer = std::make_unique<NullRenderer>();
    s.audio = std::make_unique<NullAudio>();
    return std::make_unique<Engine>(std::move(s));
}

static void testEngineBoot() {
    auto e = makeEngine(AETHER_TEST_DATA_DIR "/boot");
    CHECK(e->start());
    for (int i = 0; i < 30; i++) e->stepFrame(1.0 / 60);
    CHECK(e->scriptErrors() == 0);
    CHECK(e->tree.liveCount() > 1);
}

int main() {
    log::setSink([](LogLevel, const std::string&) {});
    testPaths();
    testStorage();
    testScene();
    testInput();
    testPhysics();
    testTimers();
    testEngineBoot();
#ifdef AETHER_AUDIO_MINIAUDIO
    testMiniaudio();
#endif
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
