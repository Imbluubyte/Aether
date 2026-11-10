// JNI bridge between the Kotlin editor and the C++ engine. Kotlin owns the GLSurfaceView / UI;
// everything that runs a game lives in the engine. One Handle = one running game session.
#include <android/log.h>
#include <jni.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "aether/audio.h"
#include "aether/engine.h"
#include "aether/gles_renderer.h"
#include "aether/log.h"
#include "android_platform.h"

using namespace aether;

namespace {

struct Handle {
    std::unique_ptr<Engine> engine;
    AndroidPlatform* platform = nullptr;  // owned by engine->svc
    std::string testScript;
    bool started = false;
    bool failed = false;
};

std::mutex g_logMutex;
std::string g_logBuf;

void installLogSink() {
    log::setSink([](LogLevel lvl, const std::string& msg) {
        int prio = lvl == LogLevel::Error ? ANDROID_LOG_ERROR : (lvl == LogLevel::Warn ? ANDROID_LOG_WARN : ANDROID_LOG_INFO);
        __android_log_write(prio, "Aether", msg.c_str());
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logBuf += (lvl == LogLevel::Error ? "[error] " : (lvl == LogLevel::Warn ? "[warn] " : "")) + msg + "\n";
        if (g_logBuf.size() > 65536) g_logBuf.erase(0, g_logBuf.size() - 32768);
    });
}

std::string str(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

Handle* H(jlong h) { return reinterpret_cast<Handle*>(h); }

}  // namespace

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_imbluubyte_aether_editor_AetherNative_##name

// Creates a session. The game (renderer, audio, scripts) is started later, on the GL thread, in surfaceCreated.
JNI(jlong, nativeCreate)(JNIEnv* env, jobject, jstring projectDir, jstring saveDir, jstring testScript) {
    installLogSink();
    auto* h = new Handle();
    Services s;
    auto platform = std::make_unique<AndroidPlatform>();
    h->platform = platform.get();
    s.platform = std::move(platform);
    s.assets = std::make_unique<DirFileSystem>(str(env, projectDir));
    s.storage = std::make_unique<DirStorage>(str(env, saveDir));
    s.renderer = std::make_unique<GLESRenderer>();
    s.audio.reset(createMiniaudioAudio(false));
    h->engine = std::make_unique<Engine>(std::move(s));
    h->testScript = str(env, testScript);
    return reinterpret_cast<jlong>(h);
}

JNI(void, nativeSurfaceCreated)(JNIEnv*, jobject, jlong handle) {
    Handle* h = H(handle);
    if (!h) return;
    if (!h->started) {
        h->started = true;
        h->failed = !h->engine->start(h->testScript);
    } else {
        h->engine->onSurfaceLost();  // GL context was recreated: old GPU handles are gone
        h->engine->onSurfaceReady();
    }
}

JNI(void, nativeSurfaceChanged)(JNIEnv*, jobject, jlong handle, jint w, jint hgt) {
    if (Handle* h = H(handle)) h->platform->setSize(w, hgt);
}

// Runs one frame. Returns 0 while running, otherwise 1 + exit code (script called aether.quit / test finished / failed to start).
JNI(jint, nativeDrawFrame)(JNIEnv*, jobject, jlong handle) {
    Handle* h = H(handle);
    if (!h || h->failed) return 2;
    if (!h->engine->running()) return 1 + h->engine->exitCode();
    h->engine->frame();
    return h->engine->running() ? 0 : 1 + h->engine->exitCode();
}

JNI(void, nativeTouch)(JNIEnv*, jobject, jlong handle, jint action, jint id, jfloat x, jfloat y) {
    if (Handle* h = H(handle)) h->platform->pushTouch(action, id, x, y);
}
JNI(void, nativeKey)(JNIEnv*, jobject, jlong handle, jboolean down, jint keycode) {
    if (Handle* h = H(handle)) h->platform->pushKey(down, keycode);
}
JNI(void, nativeAxis)(JNIEnv*, jobject, jlong handle, jint axis, jfloat value) {
    if (Handle* h = H(handle)) h->platform->pushAxis(axis, value);
}
JNI(void, nativePause)(JNIEnv*, jobject, jlong handle, jboolean paused) {
    if (Handle* h = H(handle)) {
        Event e;
        e.type = paused ? Event::Pause : Event::Resume;
        h->platform->pushEvent(e);
    }
}
JNI(void, nativeSetDebug)(JNIEnv*, jobject, jlong handle, jboolean colliders, jboolean stats) {
    if (Handle* h = H(handle)) {
        h->engine->debugColliders = colliders;
        h->engine->debugStats = stats;
    }
}
// Must be called on the GL thread.
JNI(void, nativeRestart)(JNIEnv*, jobject, jlong handle) {
    Handle* h = H(handle);
    if (h && h->started) h->failed = !h->engine->restart();
}

JNI(jstring, nativeDrainLog)(JNIEnv* env, jobject) {
    std::string out;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        out.swap(g_logBuf);
    }
    return env->NewStringUTF(out.c_str());
}

JNI(jstring, nativeStats)(JNIEnv* env, jobject, jlong handle) {
    Handle* h = H(handle);
    char buf[160] = "";
    if (h && h->started) {
        Engine& e = *h->engine;
        snprintf(buf, sizeof buf, "%.0f fps | %zu nodes | %d script errors | %s", e.fps, e.tree.liveCount(), e.scriptErrors(), e.scenePath.c_str());
    }
    return env->NewStringUTF(buf);
}

// Must be called on the GL thread, or after the context is gone (then nothing is released through GL).
JNI(void, nativeDestroy)(JNIEnv*, jobject, jlong handle) {
    Handle* h = H(handle);
    if (!h) return;
    h->engine->onSurfaceLost();
    delete h;
}
