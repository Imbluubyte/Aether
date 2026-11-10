// aether_run: run an Aether game headlessly (software renderer, deterministic clock).
//   aether_run <game_dir> [--frames N] [--seconds S] [--test test.lua] [--screenshot out.ppm]
//              [--save-dir DIR] [--size WxH] [--dt 0.016667]
// Exit code: 0 = ok, 1 = script errors / failed tests, 2 = could not start.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "aether/engine.h"
#include "aether/platform_headless.h"

using namespace aether;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: aether_run <game_dir> [--frames N] [--seconds S] [--test file.lua] [--screenshot out.ppm] [--save-dir DIR] [--size WxH] [--dt S]\n");
        return 2;
    }
    std::string dir = argv[1], test, shot, saveDir = "/tmp/aether_saves";
    int frames = 120, w = 1280, h = 720;
    double dt = 1.0 / 60.0;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--seconds") frames = (int)(std::atof(next().c_str()) / dt);
        else if (a == "--test") test = next();
        else if (a == "--screenshot") shot = next();
        else if (a == "--save-dir") saveDir = next();
        else if (a == "--dt") dt = std::atof(next().c_str());
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &w, &h);
    }
    Services s;
    auto platform = std::make_unique<HeadlessPlatform>(w, h);
    s.platform = std::move(platform);
    s.assets = std::make_unique<DirFileSystem>(dir);
    s.storage = std::make_unique<DirStorage>(saveDir);
    s.renderer = std::make_unique<SoftwareRenderer>();
    s.audio = std::make_unique<NullAudio>();
    Engine engine(std::move(s));
    if (!engine.start(test)) return 2;
    int errors;
    for (int i = 0; i < frames && engine.running(); i++) engine.stepFrame(dt);
    if (!shot.empty() && !engine.savePPM(shot)) std::fprintf(stderr, "could not write screenshot\n");
    errors = engine.scriptErrors();
    std::fprintf(stderr, "ran %llu frames, %zu nodes, %d script errors\n", (unsigned long long)engine.frameCount, engine.tree.liveCount(), errors);
    if (engine.exitCode() != 0) return engine.exitCode();
    return errors ? 1 : 0;
}
