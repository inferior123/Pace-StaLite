#ifndef STA_LOGGER_HPP
#define STA_LOGGER_HPP

#include <sstream>
#include <string>

namespace sta {

enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

class StaLogger {
public:
  StaLogger(const char *file, int line, LogLevel level);
  ~StaLogger();

  std::ostringstream &stream() { return oss_; }

  static void SetMinLevel(LogLevel level);
  static LogLevel GetMinLevel();

private:
  std::ostringstream oss_;
  LogLevel level_;
  const char *file_;
  int line_;

  static LogLevel min_level_;

  static std::string FormatTime();
  static const char *LevelTag(LogLevel level);
  static const char *Basename(const char *path);
};

} // namespace sta

#define LOG_INFO  sta::StaLogger(__FILE__, __LINE__, sta::LogLevel::INFO).stream()
#define LOG_WARN  sta::StaLogger(__FILE__, __LINE__, sta::LogLevel::WARNING).stream()
#define LOG_ERROR sta::StaLogger(__FILE__, __LINE__, sta::LogLevel::ERROR).stream()
#define LOG_DEBUG sta::StaLogger(__FILE__, __LINE__, sta::LogLevel::DEBUG).stream()

#endif // STA_LOGGER_HPP
