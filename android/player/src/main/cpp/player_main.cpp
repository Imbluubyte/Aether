// Standalone Aether player: a pure-native Android activity (no Java/Kotlin code in the APK).
// It runs the game found in the APK's assets/game/ directory with the same C++ engine + Lua runtime
// used by the editor.
#include <EGL/egl.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <memory>
#include <string>

#include "aether/audio.h"
#include "aether/engine.h"
#include "aether/gles_renderer.h"
#include "aether/log.h"
#include "android_platform.h"

using namespace aether;

namespace {

struct Player {
    android_app* app = nullptr;
    std::unique_ptr<Engine> engine;
    AndroidPlatform* platform = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    bool started = false;
    bool focused = false;
};

bool initEgl(Player& p) {
    p.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (p.display == EGL_NO_DISPLAY || !eglInitialize(p.display, nullptr, nullptr)) return false;
    const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                              EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE};
    EGLConfig config;
    EGLint n = 0;
    if (!eglChooseConfig(p.display, attribs, &config, 1, &n) || n < 1) return false;
    EGLint format = 0;
    eglGetConfigAttrib(p.display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(p.app->window, 0, 0, format);
    p.surface = eglCreateWindowSurface(p.display, config, p.app->window, nullptr);
    if (p.context == EGL_NO_CONTEXT) {
        const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        p.context = eglCreateContext(p.display, config, EGL_NO_CONTEXT, ctx);
    }
    if (p.surface == EGL_NO_SURFACE || p.context == EGL_NO_CONTEXT) return false;
    return eglMakeCurrent(p.display, p.surface, p.surface, p.context) == EGL_TRUE;
}

void destroyEglSurface(Player& p) {
    if (p.display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(p.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (p.surface != EGL_NO_SURFACE) eglDestroySurface(p.display, p.surface);
    p.surface = EGL_NO_SURFACE;
}

void destroyEgl(Player& p) {
    destroyEglSurface(p);
    if (p.context != EGL_NO_CONTEXT) eglDestroyContext(p.display, p.context);
    if (p.display != EGL_NO_DISPLAY) eglTerminate(p.display);
    p.context = EGL_NO_CONTEXT;
    p.display = EGL_NO_DISPLAY;
}

void updateSize(Player& p) {
    EGLint w = 0, h = 0;
    eglQuerySurface(p.display, p.surface, EGL_WIDTH, &w);
    eglQuerySurface(p.display, p.surface, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) p.platform->setSize(w, h);
}

void onCmd(android_app* app, int32_t cmd) {
    Player& p = *static_cast<Player*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (!app->window) break;
            if (!initEgl(p)) { log::errorf("EGL initialisation failed"); ANativeActivity_finish(app->activity); break; }
            updateSize(p);
            if (!p.started) {
                p.started = true;
                if (!p.engine->start()) { log::errorf("game failed to start"); ANativeActivity_finish(app->activity); }
            } else {
                p.engine->onSurfaceReady();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            if (p.started) p.engine->onSurfaceLost();  // the GL context dies with the surface on most devices
            destroyEgl(p);
            break;
        case APP_CMD_GAINED_FOCUS: {
            p.focused = true;
            Event e; e.type = Event::Resume; p.platform->pushEvent(e);
            break;
        }
        case APP_CMD_LOST_FOCUS: {
            p.focused = false;
            Event e; e.type = Event::Pause; p.platform->pushEvent(e);
            break;
        }
        default: break;
    }
}

int32_t onInput(android_app* app, AInputEvent* ev) {
    Player& p = *static_cast<Player*>(app->userData);
    int32_t type = AInputEvent_getType(ev);
    int32_t source = AInputEvent_getSource(ev);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if (source & AINPUT_SOURCE_JOYSTICK) {
            for (int axis : {AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y, AMOTION_EVENT_AXIS_Z, AMOTION_EVENT_AXIS_RZ, AMOTION_EVENT_AXIS_LTRIGGER,
                             AMOTION_EVENT_AXIS_RTRIGGER, AMOTION_EVENT_AXIS_HAT_X, AMOTION_EVENT_AXIS_HAT_Y})
                p.platform->pushAxis(axis, AMotionEvent_getAxisValue(ev, axis, 0));
            return 1;
        }
        if (source & AINPUT_SOURCE_TOUCHSCREEN) {
            int32_t actionRaw = AMotionEvent_getAction(ev);
            int32_t action = actionRaw & AMOTION_EVENT_ACTION_MASK;
            size_t index = (size_t)((actionRaw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            size_t count = AMotionEvent_getPointerCount(ev);
            auto push = [&](int phase, size_t i) {
                p.platform->pushTouch(phase, AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
            };
            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN: push(0, 0); break;
                case AMOTION_EVENT_ACTION_POINTER_DOWN: push(0, index); break;
                case AMOTION_EVENT_ACTION_MOVE: for (size_t i = 0; i < count; i++) push(1, i); break;
                case AMOTION_EVENT_ACTION_UP: case AMOTION_EVENT_ACTION_CANCEL: for (size_t i = 0; i < count; i++) push(2, i); break;
                case AMOTION_EVENT_ACTION_POINTER_UP: push(2, index); break;
                default: break;
            }
            return 1;
        }
    } else if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(ev);
        int32_t act = AKeyEvent_getAction(ev);
        // The system Back button stays with Android (so players can leave the game) unless it came from a gamepad.
        if (code == AKEYCODE_BACK && !(source & AINPUT_SOURCE_GAMEPAD)) return 0;
        if (act == AKEY_EVENT_ACTION_DOWN || act == AKEY_EVENT_ACTION_UP) {
            p.platform->pushKey(act == AKEY_EVENT_ACTION_DOWN, code);
            return 1;
        }
    }
    return 0;
}

}  // namespace

void android_main(android_app* app) {
    Player player;
    player.app = app;
    app->userData = &player;
    app->onAppCmd = onCmd;
    app->onInputEvent = onInput;

    log::setSink([](LogLevel lvl, const std::string& msg) {
        int prio = lvl == LogLevel::Error ? ANDROID_LOG_ERROR : (lvl == LogLevel::Warn ? ANDROID_LOG_WARN : ANDROID_LOG_INFO);
        __android_log_write(prio, "Aether", msg.c_str());
    });

    const char* dataPath = app->activity->internalDataPath ? app->activity->internalDataPath : "/data/local/tmp";
    Services s;
    auto platform = std::make_unique<AndroidPlatform>();
    player.platform = platform.get();
    platform->presentFn = [&player]() {
        if (player.display != EGL_NO_DISPLAY && player.surface != EGL_NO_SURFACE) eglSwapBuffers(player.display, player.surface);
    };
    s.platform = std::move(platform);
    s.assets = std::make_unique<AAssetFileSystem>(app->activity->assetManager, "game");
    s.storage = std::make_unique<DirStorage>(std::string(dataPath) + "/saves");
    s.renderer = std::make_unique<GLESRenderer>();
    s.audio.reset(createMiniaudioAudio(false));
    player.engine = std::make_unique<Engine>(std::move(s));

    while (!app->destroyRequested) {
        bool active = player.started && player.display != EGL_NO_DISPLAY && player.surface != EGL_NO_SURFACE && player.engine->running();
        int events;
        android_poll_source* source;
        // Block while the window is hidden so a backgrounded game uses no CPU; poll without waiting while running.
        while (ALooper_pollOnce(active ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
            active = player.started && player.display != EGL_NO_DISPLAY && player.surface != EGL_NO_SURFACE && player.engine->running();
        }
        if (app->destroyRequested) break;
        if (active) {
            updateSize(player);
            player.engine->frame();
            if (!player.engine->running()) ANativeActivity_finish(app->activity);
        }
    }
    if (player.started) player.engine->onSurfaceLost();
    player.engine.reset();
    destroyEgl(player);
}
