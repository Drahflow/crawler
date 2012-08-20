#ifndef PREFIXSET_H
#define PREFIXSET_H

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

class PrefixSet {
  public:
    void insert(const std::string &prefix) {
      prefixes.push_back(prefix);
    }

    bool matches(const std::string &s) const {
      for(auto &i: prefixes) if(!s.compare(0, i.length(), i)) return true;
      return false;
    }

  private:
    std::vector<std::string> prefixes;
};

#endif
