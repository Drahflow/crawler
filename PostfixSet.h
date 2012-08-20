#ifndef POSTFIXSET_H
#define POSTFIXSET_H

#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

class PostfixSet {
  public:
    void insert(const std::string &postfix) {
      postfixes.push_back(postfix);
    }

    bool matches(const std::string &s) const {
      for(auto &i: postfixes) if(s.length() >= i.length() && !s.compare(s.length() - i.length(), i.length(), i)) return true;
      return false;
    }

  private:
    std::vector<std::string> postfixes;
};

#endif
