#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("测试框架可用") {
  CHECK(1 + 1 == 2);
}
