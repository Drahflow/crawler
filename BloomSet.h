#ifndef BLOOMSET_H
#define BLOOMSET_H

#include <strings.h>
#include <iostream>
#include <string>

class BloomSet {
  public:
    BloomSet(uint64_t bits, uint64_t keybits, uint64_t elementbits)
      : bits(bits), keybits(keybits), elementbits(elementbits) {
      data = new unsigned char[bits / 8 + 1];

      bzero(data, bits / 8 + 1);
    }

    BloomSet(uint64_t expectedElements) {
      if(expectedElements == 0) expectedElements = 2;

      // let's aim for ~0.0001 probability
      bits = expectedElements * 20;
      elementbits = keybits = 7 * bits / expectedElements / 10;
      data = new unsigned char[bits / 8 + 1];

      bzero(data, bits / 8 + 1);
    }

    ~BloomSet() {
      delete[] data;
    }

    void insert(const char *str, const size_t n) { setBits(str, n, 1); }
    void insert(const std::string &s) { insert(s.c_str(), s.length()); }
    void remove(const char *str, const size_t n) { setBits(str, n, 0); }
    void remove(const std::string &s) { remove(s.c_str(), s.length()); }

    bool contains(const char *str, const size_t n) {
      return countBits(str, n) >= elementbits;
    }
    bool contains(const std::string &s) { return contains(s.c_str(), s.length()); }

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
    uint64_t elementbits;
    unsigned char *data;

    void setBits(const char *str, const size_t n, unsigned char bit) {
      int todo;
      if(bit) {
        todo = elementbits - countBits(str, n);
      } else {
        todo = countBits(str, n) - (keybits - elementbits - 1);
      }
      
      todo += 8;

//      if(todo > 0) {
//        std::cerr << todo << keybits << elementbits << static_cast<int>(bit)
//          << countBits(str, n) << std::endl;
//      }
      uint64_t index = 0;
      for(unsigned int i = 0; todo > 0; ++i) {
        index = nextBit(index, str, n, i);
        //std::cerr << "looking at bit " << (index % bits) << std::endl;
        if(!!(data[(index % bits) / 8] & (1 << ((index % bits) % 8))) != bit) {
          data[(index % bits) / 8] ^= 1 << ((index % bits) % 8);
          --todo;
        }
        if(i >= keybits) {
          // hit the same bit multiple times
          break;
        }
      }
    }

    unsigned int countBits(const char *str, const size_t n) {
      unsigned int count = 0;
      uint64_t index = 0;
      for(unsigned int i = 0; i < keybits; ++i) {
        index = nextBit(index, str, n, i);
        count += !!(data[(index % bits) / 8] & (1 << ((index % bits) % 8)));
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
      for(size_t i = 0; i < n; ++str, ++i) {
        //lastBit = *str * magic[bitNo % 8] + (lastBit >> 8);
        lastBit = (lastBit ^ 17) + *str * magic[bitNo % 8] + (lastBit >> 8);
      }

      return lastBit;
    }
};

#endif
