#ifndef DFTRACER_COMMON_UTILS_H
#define DFTRACER_COMMON_UTILS_H

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>

namespace dftracer {

inline bool parse_positive_size_t(const char* value, size_t* parsed) {
  if (!value || !parsed || value[0] == '\0') return false;

  errno = 0;
  char* end = nullptr;
  unsigned long long v = std::strtoull(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || v == 0 ||
      v > std::numeric_limits<size_t>::max()) {
    return false;
  }

  *parsed = static_cast<size_t>(v);
  return true;
}

inline std::string trim_copy(const std::string& value) {
  if (value.empty()) return value;
  size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  if (first == value.size()) return std::string{};
  size_t last = value.size() - 1;
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last]))) {
    --last;
  }
  return value.substr(first, last - first + 1);
}

}  // namespace dftracer

#endif  // DFTRACER_COMMON_UTILS_H
