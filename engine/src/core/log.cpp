#include "aether/log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace aether::log {

static std::mutex g_mutex;
static Sink g_sink;

void setSink(Sink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

void write(LogLevel level, const std::string& msg) {
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        sink = g_sink;
    }
    if (sink) {
        sink(level, msg);
        return;
    }
#ifdef __ANDROID__
    int prio = level == LogLevel::Error ? ANDROID_LOG_ERROR : (level == LogLevel::Warn ? ANDROID_LOG_WARN : ANDROID_LOG_INFO);
    __android_log_write(prio, "Aether", msg.c_str());
#else
    const char* tag = level == LogLevel::Error ? "[error] " : (level == LogLevel::Warn ? "[warn] " : "");
    std::fprintf(stderr, "%s%s\n", tag, msg.c_str());
#endif
}

static void vwrite(LogLevel level, const char* fmt, va_list ap) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    write(level, buf);
}

void infof(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vwrite(LogLevel::Info, fmt, ap); va_end(ap); }
void warnf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vwrite(LogLevel::Warn, fmt, ap); va_end(ap); }
void errorf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vwrite(LogLevel::Error, fmt, ap); va_end(ap); }

}  // namespace aether::log
