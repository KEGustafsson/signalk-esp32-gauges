#pragma once

// A minimal test runner: TEST(name) { CHECK(...); } -- no dependencies.

#include <math.h>
#include <stdio.h>

#include <functional>
#include <vector>

namespace test {

struct Case {
  const char* name;
  std::function<void()> body;
};

std::vector<Case>& cases();
extern int failures;

struct Register {
  Register(const char* name, std::function<void()> body) {
    cases().push_back({name, std::move(body)});
  }
};

inline bool near(double a, double b, double tolerance) {
  return fabs(a - b) <= tolerance;
}

}  // namespace test

#define TEST(name)                                          \
  static void name();                                       \
  static test::Register name##_registration(#name, name);   \
  static void name()

#define CHECK(condition)                                                  \
  do {                                                                    \
    if (!(condition)) {                                                   \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
      test::failures++;                                                   \
    }                                                                     \
  } while (0)

#define CHECK_NEAR(a, b, tolerance)                                       \
  do {                                                                    \
    const double check_a = (a), check_b = (b);                            \
    if (!test::near(check_a, check_b, (tolerance))) {                     \
      printf("  FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, \
             #a, check_a, check_b);                                       \
      test::failures++;                                                   \
    }                                                                     \
  } while (0)
