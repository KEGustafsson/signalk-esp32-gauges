#include "test.h"

namespace test {

std::vector<Case>& cases() {
  static std::vector<Case> all;
  return all;
}

int failures = 0;

}  // namespace test

int main() {
  for (const test::Case& c : test::cases()) {
    const int before = test::failures;
    c.body();
    printf("%s %s\n", test::failures == before ? "ok  " : "FAIL", c.name);
  }
  printf("%zu tests, %d failed checks\n", test::cases().size(),
         test::failures);
  return test::failures == 0 ? 0 : 1;
}
