// Engine: owns every subsystem and runs the frame loop. Platform-independent; the platform,
// renderer, audio and file systems are injected so the same core runs on Android, in the headless
// test runner and (future) on Nintendo Switch.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "aether/audio.h"
#include "aether/filesystem.h"
#include "aether/font.h"
#include "aether/input.h"
#include "aether/physics.h"
#include "aether/platform.h"
#include "aether/renderer.h"
#include "aether/resources.h"
#include "aether/scene.h"
#include "aether/timers.h"

namespace aether {

class LuaVM;

struct GameConfig {
    std::string title = "Aether Game";
    int width = 1280;     // design resolution (virtual pixels)
    int height = 720;
    std::string stretch = "expand";  // "expand" (keep height, widen) or "fit" (letterbox)
    std::string mainScript = "main.lua";
    Color background{0.07f, 0.08f, 0.11f, 1};
    bool smooth = false;  // linear texture filtering (default: crisp pixels)
    float fixedHz = 60;
};

struct Camera {
    Vec2 pos;
    float zoom = 1;
    NodeId follow = kNoNode;
    float smoothing = 0;  // 0 = snap, otherwise larger = snappier easing
    float shakeAmp = 0, shakeTime = 0, shakeDur = 0;
    Vec2 shakeOffset;
};

struct Services {
    std::unique_ptr<IPlatform> platform;
    std::unique_ptr<IFileSystem> assets;
    std::unique_ptr<IStorage> storage;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<IAudio> audio;
};

class Engine {
public:
    explicit Engine(Services services);
    ~Engine();

    // Loads game.lua (optional), initialises the VM and runs the main script.
    // `testScript` (optional) is run by the Lua test harness after the game starts.
    bool start(const std::string& testScript = "");
    // One frame using the platform clock.
    void frame();
    // One frame with an explicit time step (deterministic; used by tests and the headless runner).
    void stepFrame(double dt);
    // Tear down the VM and scene and run the game again from its main script.
    bool restart();
    void shutdown();

    bool running() const { return running_ && !quit_; }
    void requestQuit(int exitCode = 0) { quit_ = true; exitCode_ = exitCode; }
    int exitCode() const { return exitCode_; }
    // Number of Lua errors logged since start (syntax errors, runtime errors in callbacks).
    int scriptErrors() const;
    // Tell the engine the surface/context was recreated (Android).
    void onSurfaceLost();
    void onSurfaceReady();

    // --- subsystems (public for the script bindings) ---
    Services svc;
    GameConfig config;
    SceneTree tree;
    Physics physics;
    Input input;
    Camera camera;
    Timers timers;
    ResourceManager resources;
    Font font;
    SpriteBatch batch;
    std::unique_ptr<LuaVM> vm;

    // --- state exposed to scripts ---
    double time = 0;          // scaled game time
    uint64_t frameCount = 0;
    float timeScale = 1;
    bool paused = false;
    bool debugColliders = false;
    bool debugStats = false;
    float fps = 0;
    float virtW = 1280, virtH = 720;
    std::string pendingScene;   // path set by aether.scene.change; applied after the update phase
    std::string scenePath;
    bool testMode = false;
    bool drawUi = false;      // true while draw_ui callbacks run (screen-space immediate drawing)

    // Screenshot of the current software-rendered frame as PNG-less PPM (P6). Returns false if the
    // renderer cannot read pixels back.
    bool savePPM(const std::string& path) const;

    Vec2 screenToWorld(Vec2 s) const;
    Vec2 worldToScreen(Vec2 w) const;

private:
    void computeView(int& vpX, int& vpY, int& vpW, int& vpH);
    void render();
    void applyPendingScene();
    bool loadConfig();
    void updateAnimations(float dt);
    void updateCamera(float dt);
    void drawNodes(bool screenSpace);
    void drawVirtualControls();
    void drawDebug();
    void drawCircle(Vec2 c, float r, float thickness, Color col);

    bool running_ = false;
    bool quit_ = false;
    int exitCode_ = 0;
    double lastPlatformTime_ = -1;
    float fixedAccum_ = 0;
    float fpsAccum_ = 0;
    int fpsFrames_ = 0;
    double fpsWindowStart_ = 0;
    std::vector<Event> events_;
    int vpX_ = 0, vpY_ = 0, vpW_ = 1, vpH_ = 1;
    float viewScale_ = 1;
    std::string testScript_;
};

}  // namespace aether
