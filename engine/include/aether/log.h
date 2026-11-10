// Minimal logging with a replaceable sink (used by the Android editor console and tests).
#pragma once
#include <functional>
#include <string>

namespace aether {

enum class LogLevel { Info, Warn, Error };

namespace log {
using Sink = std::function<void(LogLevel, const std::string&)>;
// Replace the sink. Pass nullptr to restore the default (stderr / logcat).
void setSink(Sink sink);
void write(LogLevel level, const std::string& msg);
void infof(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warnf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void errorf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
}  // namespace log

}  // namespace aether
