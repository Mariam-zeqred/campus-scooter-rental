// A stand-in for the Unity test framework, just big enough for
// firmware/test/test_core/test_main.cpp. `pio test -e native` uses the real
// Unity; tools/check.py uses this header so the same tests run with any host
// C++ compiler and no PlatformIO download.
#pragma once
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace unity_shim {
inline int& failures() { static int n = 0; return n; }
inline int& tests() { static int n = 0; return n; }
inline jmp_buf& jump() { static jmp_buf j; return j; }
inline void fail(const char* file, int line, const char* what) {
  printf("%s:%d: FAIL: %s\n", file, line, what);
  longjmp(jump(), 1);
}
inline void run(void (*fn)(), const char* name) {
  ++tests();
  if (setjmp(jump()) == 0) {
    fn();
    printf("PASS %s\n", name);
  } else {
    ++failures();
    printf("FAIL %s\n", name);
  }
}
inline int finish() {
  printf("-----------------------\n%d Tests %d Failures\n%s\n", tests(), failures(), failures() ? "FAIL" : "OK");
  return failures() ? 1 : 0;
}
}  // namespace unity_shim

#define UNITY_BEGIN() (void)0
#define UNITY_END() unity_shim::finish()
#define RUN_TEST(fn) unity_shim::run(fn, #fn)
#define TEST_ASSERT_TRUE(c) do { if (!(c)) unity_shim::fail(__FILE__, __LINE__, #c " is false"); } while (0)
#define TEST_ASSERT_FALSE(c) do { if (c) unity_shim::fail(__FILE__, __LINE__, #c " is true"); } while (0)
#define TEST_ASSERT_EQUAL_INT(e, a) do { if (static_cast<long>(e) != static_cast<long>(a)) { \
  char m[160]; snprintf(m, sizeof(m), "expected %ld, got %ld (%s)", static_cast<long>(e), static_cast<long>(a), #a); \
  unity_shim::fail(__FILE__, __LINE__, m); } } while (0)
#define TEST_ASSERT_EQUAL_STRING(e, a) do { if (strcmp((e), (a)) != 0) { \
  char m[700]; snprintf(m, sizeof(m), "expected\n  %s\ngot\n  %s", (e), (a)); \
  unity_shim::fail(__FILE__, __LINE__, m); } } while (0)
#define TEST_ASSERT_FLOAT_WITHIN(d, e, a) do { if (fabs(static_cast<double>(e) - static_cast<double>(a)) > static_cast<double>(d)) { \
  char m[160]; snprintf(m, sizeof(m), "expected %f +/- %f, got %f (%s)", static_cast<double>(e), static_cast<double>(d), static_cast<double>(a), #a); \
  unity_shim::fail(__FILE__, __LINE__, m); } } while (0)
