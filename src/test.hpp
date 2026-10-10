#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace caracal::test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Register {
  Register(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int failures = 0;
inline int checks = 0;

inline void check(bool cond, const char* expr, const char* file, int line) {
  ++checks;
  if (!cond) {
    ++failures;
    std::cerr << "FAIL " << file << ":" << line << " " << expr << "\n";
  }
}

}  // namespace caracal::test

#define TEST(name)                                          \
  static void name();                                       \
  static ::caracal::test::Register reg_##name(#name, name); \
  static void name()

#define CHECK(cond) \
  ::caracal::test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
