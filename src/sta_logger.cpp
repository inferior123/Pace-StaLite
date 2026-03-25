#include "sta/sta_logger.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unistd.h>

namespace sta {

LogLevel StaLogger::min_level_ = LogLevel::INFO;
static std::mutex log_mutex;

StaLogger::StaLogger(const char *file, int line, LogLevel level)
    : level_(level), file_(file), line_(line) {}

StaLogger::~StaLogger() {
  if (level_ < min_level_) {
    return;
  }

  std::string msg = oss_.str();
  if (msg.empty()) {
    return;
  }

  std::string line = std::string(LevelTag(level_)) +
                     FormatTime() + "  " +
                     std::to_string(gettid()) + " " +
                     Basename(file_) + ":" +
                     std::to_string(line_) + "] " +
                     msg;

  std::lock_guard<std::mutex> lock(log_mutex);
  fprintf(stderr, "%s\n", line.c_str());
  fflush(stderr);
}

void StaLogger::SetMinLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(log_mutex);
  min_level_ = level;
}

LogLevel StaLogger::GetMinLevel() {
  std::lock_guard<std::mutex> lock(log_mutex);
  return min_level_;
}

std::string StaLogger::FormatTime() {
  auto now = std::chrono::system_clock::now();
  auto epoch = now.time_since_epoch();
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(epoch);
  auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(epoch - secs);

  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf;
  localtime_r(&t, &tm_buf);

  char buf[32];
  snprintf(buf, sizeof(buf), "%04d%02d%02d %02d:%02d:%02d.%06ld",
           tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
           tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
           static_cast<long>(usecs.count()));
  return buf;
}

const char *StaLogger::LevelTag(LogLevel level) {
  switch (level) {
  case LogLevel::DEBUG:   return "D";
  case LogLevel::INFO:    return "I";
  case LogLevel::WARNING: return "W";
  case LogLevel::ERROR:   return "E";
  }
  return "?";
}

const char *StaLogger::Basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

} // namespace sta
