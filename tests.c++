#include "BloomSet.h"

#include <cassert>

int main(void) {
  BloomSet set(1024);

  assert(!set.contains("abcd", 4));
  assert(!set.contains("abce", 4));
  assert(!set.contains("abcf", 4));

  set.insert("abce", 4);

  assert(!set.contains("abcd", 4));
  assert(set.contains("abce", 4));
  assert(!set.contains("abcf", 4));

  return 0;
}
