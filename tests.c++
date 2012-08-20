#include "BloomSet.h"
#include "PrefixSet.h"
#include "PostfixSet.h"

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

  PrefixSet prefix;

  prefix.insert("/log/");
  prefix.insert("/blog");

  assert(prefix.matches("/log/abcdef"));
  assert(!prefix.matches("/logabcdef"));
  assert(!prefix.matches("logabcdef"));
  assert(!prefix.matches("/blorg"));
  assert(prefix.matches("/blog/what"));
  assert(prefix.matches("/blogwhat"));

  PostfixSet postfix;

  postfix.insert(".xyz");

  assert(postfix.matches("/testing.xyz"));
  assert(postfix.matches("whatever.xyz"));
  assert(!postfix.matches("someother"));

  postfix.insert("");
  assert(postfix.matches("someother"));
  assert(postfix.matches(""));

  return 0;
}
