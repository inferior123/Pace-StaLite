/**
 * Minimal compatibility layer for lib parser (replacing iEDA Vector, BTreeMap, StrMap, Log, Str).
 */
#ifndef PBA_STA_LIB_PARSER_ISTA_COMPAT_HPP
#define PBA_STA_LIB_PARSER_ISTA_COMPAT_HPP

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <optional>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Container aliases (replace Vector.hh, BTreeMap.hh, StrMap.hh, BTreeSet.hh)
// -----------------------------------------------------------------------------
namespace ista {

template <typename T>
using Vector = std::vector<T>;

template <typename K, typename V>
using BTreeMap = std::map<K, V>;

template <typename K>
using BTreeSet = std::set<K>;

template <typename V>
using StrMap = std::map<std::string, V>;

}  // namespace ista

// -----------------------------------------------------------------------------
// FORBIDDEN_COPY
// -----------------------------------------------------------------------------
#define FORBIDDEN_COPY(ClassName)           \
  ClassName(const ClassName&) = delete;     \
  ClassName& operator=(const ClassName&) = delete;

// -----------------------------------------------------------------------------
// Log macros (replace log/Log.hh)
// -----------------------------------------------------------------------------
#define LOG_FATAL ::ista::_impl::FatalStream()
#define LOG_INFO  ::ista::_impl::InfoStream()
#define LOG_FATAL_IF(cond) \
  if (cond) ::ista::_impl::FatalStream()
#define DLOG_FATAL ::ista::_impl::FatalStream()
#define DLOG_FATAL_IF(cond) \
  if (cond) ::ista::_impl::FatalStream()
#define LOG_ERROR_FIRST_N(n) ::ista::_impl::ErrorFirstNStream(n)
#define LOG_INFO_EVERY_N(n)   ::ista::_impl::InfoEveryNStream(n)
#define DLOG_INFO_EVERY_N(n)  ::ista::_impl::InfoEveryNStream(n)

namespace ista {
namespace _impl {

struct FatalStream {
  template <typename T>
  FatalStream& operator<<(const T& x) {
    std::cerr << x;
    return *this;
  }
  ~FatalStream() {
    std::cerr << std::endl;
    std::abort();
  }
};

struct InfoStream {
  template <typename T>
  InfoStream& operator<<(const T& x) {
    std::cerr << x;
    return *this;
  }
  ~InfoStream() { std::cerr << std::endl; }
};

class ErrorFirstNStream {
 public:
  explicit ErrorFirstNStream(unsigned n) : n_(n), count_(0) {}
  template <typename T>
  ErrorFirstNStream& operator<<(const T& x) {
    if (count_ < n_) {
      std::cerr << x;
      ++count_;
    }
    return *this;
  }
  ~ErrorFirstNStream() {
    if (count_ < n_) std::cerr << std::endl;
  }

 private:
  unsigned n_;
  mutable unsigned count_;
};

class InfoEveryNStream {
 public:
  explicit InfoEveryNStream(unsigned n) : n_(n), count_(0) {}
  template <typename T>
  InfoEveryNStream& operator<<(const T& x) {
    if (count_ % n_ == 0) std::cerr << x;
    ++count_;
    return *this;
  }
  ~InfoEveryNStream() { std::cerr << std::endl; }

 private:
  unsigned n_;
  mutable unsigned count_;
};

}  // namespace _impl
}  // namespace ista

// -----------------------------------------------------------------------------
// Str namespace (replace string/Str.hh)
// -----------------------------------------------------------------------------
namespace ista {

struct Str {
  static bool equal(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
  }

  static bool noCaseEqual(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
      if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
        return false;
      ++a;
      ++b;
    }
    return *a == *b;
  }

  static std::vector<std::string> split(const std::string& s, const std::string& delim) {
    std::vector<std::string> out;
    std::string t = s;
    size_t pos;
    while ((pos = t.find(delim)) != std::string::npos) {
      out.push_back(t.substr(0, pos));
      t = t.substr(pos + delim.size());
    }
    if (!t.empty()) out.push_back(t);
    return out;
  }

  static bool startWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
  }

  /** Regex capture groups; group 0 is full match, 1,2,... are captures. */
  static std::vector<std::string> matchPattern(const std::string& s, const std::string& regex_pattern) {
    std::vector<std::string> out;
    std::regex re(regex_pattern);
    std::smatch m;
    if (std::regex_search(s, m, re)) {
      for (size_t i = 0; i < m.size(); ++i)
        out.push_back(m.str(i));
    }
    return out;
  }

  /** Parse "bus_name[index]" or "bus_name[lo:hi]" -> (bus_name, optional_index). */
  static std::pair<std::string, std::optional<int>> matchBusName(const std::string& port_name) {
    std::string name;
    std::optional<int> index;
    size_t bracket = port_name.find('[');
    if (bracket == std::string::npos) {
      return {port_name, std::nullopt};
    }
    name = port_name.substr(0, bracket);
    size_t end = port_name.find(']', bracket);
    if (end == std::string::npos) return {port_name, std::nullopt};
    std::string inner = port_name.substr(bracket + 1, end - bracket - 1);
    size_t colon = inner.find(':');
    if (colon != std::string::npos) {
      return {name, std::nullopt};
    }
    try {
      index = std::stoi(inner);
    } catch (...) {
      return {name, std::nullopt};
    }
    return {name, index};
  }

  static std::string printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
  }

  /** Remove backslash from escaped identifiers (e.g. "\foo" -> "foo"). */
  static std::string concateBackSlashStr(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\\' && i + 1 < s.size())
        ++i;
      out += s[i];
    }
    return out;
  }
};

}  // namespace ista

#endif
