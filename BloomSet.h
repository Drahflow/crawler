#ifndef BLOOMSET_H
#define BLOOMSET_H

#include <strings.h>
#include <iostream>
#include <string>

class BloomSet {
  public:
    BloomSet(uint64_t expectedElements) {
      if(expectedElements == 0) expectedElements = 2;

      // let's aim for ~0.0001 probability
      bits = expectedElements * 20;
      keybits = 7 * bits / expectedElements / 10;
      data = new unsigned char[bits / 8 + 1];

      bzero(data, bits / 8 + 1);
    }

    ~BloomSet() {
      delete[] data;
    }

    bool contains(const char *str, const size_t n) { return countBits(str, n) >= keybits; }
    bool contains(const std::string &s) { return contains(s.c_str(), s.length()); }

    // return true if the element already existed
    bool insert(const char *str, const size_t n) { return setBits(str, n) >= keybits; }
    bool insert(const std::string &s) { return insert(s.c_str(), s.length()); }

    int estimateFill() {
      int fill = 0;
      unsigned int max = 256;
      if(bits / 8 < max) max = bits / 8;

      for(unsigned int i = 0; i < max; ++i) fill += data[i] & 1;
      return (fill * 1000) / max;
    }

  private:
    uint64_t bits;
    uint64_t keybits;
    unsigned char *data;

    unsigned int setBits(const char *str, const size_t n) {
      unsigned int count = 0;
      uint64_t index = 0;

      for(unsigned int i = 0; i < keybits; ++i) {
        index = nextBit(index, str, n, i);
        count +=!! (data[(index % bits) / 8] & (1 << ((index % bits) % 8)));
        data[(index % bits) / 8] |= 1 << ((index % bits) % 8);
      }

      return count;
    }

    unsigned int countBits(const char *str, const size_t n) {
      unsigned int count = 0;
      uint64_t index = 0;

      for(unsigned int i = 0; i < keybits; ++i) {
        index = nextBit(index, str, n, i);
        count +=!! (data[(index % bits) / 8] & (1 << ((index % bits) % 8)));
      }

      return count;
    }

    uint64_t nextBit(uint64_t lastBit, const char *str, const size_t n, char bitNo) {
      uint64_t magic[] = {
        0xF567789806898063ull,
        0x4567779816798165ull,
        0xC567769826698267ull,
        0x4567759836598369ull,
        0xD567749846498461ull,
        0x4567739856398563ull,
        0xE567729866298667ull,
        0x4567719876198765ull,
      };
      const char *e = str + n;

      while(str < e - 7) {
        lastBit = (lastBit ^ 17) + *reinterpret_cast<const uint64_t *>(str) * magic[bitNo % 8] + (lastBit >> 8);
        str += 8;
      }
      if(str < e - 3) {
        lastBit = (lastBit ^ 17) + *reinterpret_cast<const uint32_t *>(str) * magic[bitNo % 8] + (lastBit >> 8);
        str += 4;
      }
      if(str < e - 1) {
        lastBit = (lastBit ^ 17) + *reinterpret_cast<const uint16_t *>(str) * magic[bitNo % 8] + (lastBit >> 8);
        str += 2;
      }
      if(str < e) {
        lastBit = (lastBit ^ 17) + *reinterpret_cast<const uint8_t *>(str) * magic[bitNo % 8] + (lastBit >> 8);
      }

      return lastBit;
    }
};

#endif
