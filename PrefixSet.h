#ifndef PREFIXSET_H
#define PREFIXSET_H

#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

class PrefixSet {
  public:
    void insert(const std::string &prefix) {
      prefixes.push_back(prefix);
    }

    bool matches(const std::string &s) const {
      for(auto &i: prefixes) if(!i.compare(0, i.length(), s)) return true;
      return false;
    }

  private:
    std::vector<std::string> prefixes;
};

#endif
