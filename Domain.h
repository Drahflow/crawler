#ifndef DOMAIN_H
#define DOMAIN_H

#include "DomainStream.h"
#include "PrefixSet.h"
#include "PostfixSet.h"
#include "BloomSet.h"

#include <stdint.h>
#include <vector>
#include <list>
#include <string>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <netinet/ip.h>
#include <cassert>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

class Domain {
  public:
    Domain(const std::string &url): socket(0), inBuffer(0), outBuffer(0) {
      hostname = extractHost(url);
      searchFront.push_back("/robots.txt");
      searchFront.push_back(extractPath(url));
      robotsTxtActive = true;
      robotsTxtRelevant = true;
      reportDownloaded = 0;
      reportDownloadedNew = 0;

      maximalUrlLength = 256;
      maximalDownloaded = 2000000;
      
      gettimeofday(&lastActivity, 0);
    }

    void setRemainingFetches(uint64_t n) {
      remainingFetches = n;

      while(remainingFetches < searchFront.size()) searchFront.pop_back();
      remainingFetches -= searchFront.size();
    }

    void setCooldownMilliseconds(uint64_t ms) {
      cooldownMilliseconds = ms;
    }

    void setIp(uint32_t addr) {
      ip = addr;
    }

    void setSeenLines(BloomSet *lines) {
      seenLines = lines;

      seenUrls = new BloomSet(remainingFetches);
      for(auto &url: searchFront) seenUrls->insert(url);
    }

    void setIgnoreList(PostfixSet *ignore) {
      ignoreList = ignore;
    }

    const std::string &getHostname() const {
      return hostname;
    }

    std::string getIpString() const {
      std::ostringstream out;

      out << ((ip      ) & 255) << '.'
          << ((ip >>  8) & 255) << '.'
          << ((ip >> 16) & 255) << '.'
          << ((ip >> 24) & 255);

      return out.str();
    }

    template<class A> void startDownloading(const A &add) {
      if(searchFront.empty()) return;

      output = new DomainStream("data/" + hostname);

      inBuffer = new char[BUFFER_SIZE];
      outBuffer = new char[BUFFER_SIZE];

      openSocket(add);
      startRequest();
    }

    void finishDownloading() {
      if(!inBuffer) return;
      delete[] inBuffer;
      inBuffer = 0;

      assert(outBuffer);
      delete[] outBuffer;
      outBuffer = 0;

      assert(seenUrls);
      delete seenUrls;
      seenUrls = 0;

      assert(output);
      delete output;
      output = 0;
    }

    template<class A, class M, class D, class F> void handleInput(const A &add, const M &, const D &del, const F &finish) {
      gettimeofday(&lastActivity, 0);

      if(inBufferFill == inBuffer + BUFFER_SIZE) {
        // Yes, this looses data in very long lines.

        memmove(inBuffer, inBufferPos, inBufferFill - inBufferPos);

        inBufferFill -= inBufferPos - inBuffer;
        inBufferPos = inBuffer;
      }

      ssize_t len = read(socket, inBufferFill, inBuffer + BUFFER_SIZE - inBufferFill);

      if(len < 0) {
        std::cerr << "read failed in weird ways: " + std::string(strerror(errno)) << std::endl;
        handleEnd(add, del, finish);
      } else if(len == 0) {
        handleEnd(add, del, finish);
      } else {
        reportDownloaded += len;
        currentDownloaded += len;

        if(currentDownloaded > maximalDownloaded) {
          std::cerr << "File was too large: " << searchFront.front() << std::endl;
          handleEnd(add, del, finish);
          return;
        }

        inBufferFill += len;

        for(char *s = inBufferPos; s != inBufferFill; ++s) {
          // uint64_t v = *reinterpret_cast<uint64_t *>(s);
          // if(s < inBufferFill - 8 &&
          //    (v & 0xff00000000000000ull) != 0x0a00000000000000ull &&
          //    (v & 0x00ff000000000000ull) != 0x000a000000000000ull &&
          //    (v & 0x0000ff0000000000ull) != 0x00000a0000000000ull &&
          //    (v & 0x000000ff00000000ull) != 0x0000000a00000000ull &&
          //    (v & 0x00000000ff000000ull) != 0x000000000a000000ull &&
          //    (v & 0x0000000000ff0000ull) != 0x00000000000a0000ull &&
          //    (v & 0x000000000000ff00ull) != 0x0000000000000a00ull &&
          //    (v & 0x00000000000000ffull) != 0x000000000000000aull) {
          //   s += 7; continue;
          // }
          if(s < inBufferFill - 4) {
            uint32_t v = *reinterpret_cast<uint32_t *>(s);
            if((v & 0xff000000ul) != 0xa000000ul &&
               (v & 0x00ff0000ul) != 0x00a0000ul &&
               (v & 0x0000ff00ul) != 0x0000a00ul &&
               (v & 0x000000fful) != 0x000000aul) {
              s += 3; continue;
            }
          }

          if(*s == '\n') {
            // (this->*(robotsTxtActive? &Domain::handleRobotsTxtLine: &Domain::handleLine))(inBufferPos, s);
            if(robotsTxtActive) handleRobotsTxtLine(inBufferPos, s + 1);
            if(!robotsTxtActive) handleLine(inBufferPos, s + 1);

            inBufferPos = s + 1;
          }
        }
      }
    }

    template<class A, class M, class D, class F> void handleError(const A &add, const M &, const D &del, const F &finish) {
      handleEnd(add, del, finish);
    }

    template<class A, class D, class F> void handleEnd(const A &add, const D &del, const F &finish) {
      if(socket) {
        finishRequest();
        closeSocket(del);
      }

      if(searchFront.empty()) {
        finishDownloading();
        finish();
        return;
      }

      if(cooldownMilliseconds == 0) {
        openSocket(add);
        startRequest();
      }
    }

    template<class A, class M, class D, class F> void handleLoop(const A &add, const M &mod, const D &del, const F &finish) {
      timeval now;
      gettimeofday(&now, 0);

      if(!socket && !searchFront.empty()) {
        if(static_cast<uint64_t>(1000 * (now.tv_sec - lastActivity.tv_sec) + (now.tv_usec - lastActivity.tv_usec) / 1000)
            > cooldownMilliseconds) {
          openSocket(add);
          startRequest();

          gettimeofday(&lastActivity, 0);
        }
      } else if(now.tv_sec - lastActivity.tv_sec > 60) {
        handleError(add, mod, del, finish);
        gettimeofday(&lastActivity, 0);
      }
    }

    template<class A, class M, class D, class F> void handleOutput(const A &add, const M &mod, const D &del, const F &finish) {
      assert(outBufferPos != outBufferFill);

      ssize_t len = write(socket, outBufferPos, outBufferFill - outBufferPos);
      if(len < 0) {
        std::cerr << hostname << ": write failed: " << std::string(strerror(errno)) << std::endl;
        handleEnd(add, del, finish);
        return;
      }

      outBufferPos += len;

      if(outBufferPos == outBufferFill) mod(socket, true, false);
    }

    void report() {
      std::cout << "[" <<
        std::setw(10) << reportDownloaded << " b/s | " <<
        std::setw(10) << reportDownloadedNew << " b/s ], " <<
        std::setw(8) << remainingFetches << " | " <<
        std::setw(8) << searchFront.size() << " -- " <<
        hostname << (searchFront.empty()? "": searchFront.front())
        << std::endl;

      reportDownloaded = 0;
      reportDownloadedNew = 0;
    }

  private:
    static const int BUFFER_SIZE = 1024 * 64;

    std::string hostname;
    uint32_t ip;

    uint64_t cooldownMilliseconds;
    uint64_t nextFetchTime;
    uint64_t remainingFetches;
    DomainStream *output;
    std::list<std::string> searchFront;
    bool robotsTxtActive;
    bool robotsTxtRelevant;
    PrefixSet robotsTxt;
    PostfixSet *ignoreList;

    int socket;

    char *inBuffer;
    char *inBufferPos;
    char *inBufferFill;

    char *outBuffer;
    char *outBufferPos;
    char *outBufferFill;

    timeval lastActivity;

    BloomSet *seenUrls;
    BloomSet *seenLines;

    uint64_t reportDownloaded;
    uint64_t reportDownloadedNew;

    uint64_t currentDownloaded;
    int64_t maximalUrlLength;
    uint64_t maximalDownloaded;

    std::string extractHost(const std::string &url) {
      std::string::const_iterator s, e;

      for(s = url.begin(); s != url.end() && *s != ':'; ++s);
      if(s == url.end() || *s != ':') throw std::runtime_error("Invalid url: " + url);
      ++s;
      if(s == url.end() || *s != '/') throw std::runtime_error("Invalid url: " + url);
      ++s;
      if(s == url.end() || *s != '/') throw std::runtime_error("Invalid url: " + url);
      ++s;
      for(e = s; e != url.end() && *e != '/'; ++e);

      return std::string(s, e);
    }

    std::string extractPath(const std::string &url) {
      std::string::const_iterator s, e;

      for(s = url.begin(); s != url.end() && *s != ':'; ++s);
      if(s == url.end() || *s != ':') throw std::runtime_error("Invalid url: " + url);
      ++s;
      if(s == url.end() || *s != '/') throw std::runtime_error("Invalid url: " + url);
      ++s;
      if(s == url.end() || *s != '/') throw std::runtime_error("Invalid url: " + url);
      ++s;
      for(e = s; e != url.end() && *e != '/'; ++e);

      return std::string(e, url.end());
    }

    template<class A> void openSocket(const A &add) {
      assert(!socket);

      sockaddr_in addr { AF_INET, 80 << 8, { ip }};
      socket = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
      connect(socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

      inBufferPos = inBufferFill = inBuffer;
      outBufferPos = outBufferFill = outBuffer;

      add(socket, false, true);
    }

    template<class D> void closeSocket(const D &del) {
      assert(socket);

      del(socket, false, false);
      close(socket);
      socket = 0;
    }

    void startRequest() {
      assert(outBufferPos == outBufferFill);
      assert(!searchFront.empty());

      outBufferPos = outBufferFill = outBuffer;

      for(const char *s = "GET "; (*outBufferFill = *s++); outBufferFill++);
      for(const char *s = searchFront.front().c_str(); (*outBufferFill = *s++); outBufferFill++);
      for(const char *s = " HTTP/1.1\r\n"; (*outBufferFill = *s++); outBufferFill++);
      for(const char *s = "Host: "; (*outBufferFill = *s++); outBufferFill++);
      for(const char *s = hostname.c_str(); (*outBufferFill = *s++); outBufferFill++);
      for(const char *s = "\r\nConnection: close\r\n\r\n"; (*outBufferFill = *s++); outBufferFill++);

      // std::cerr << "Fetching: " << searchFront.front() << std::endl;
      output->handleRequest(hostname, searchFront.front().c_str(), searchFront.front().c_str() + searchFront.front().length());
      currentDownloaded = 0;
    }

    void finishRequest() {
      assert(!searchFront.empty());

      searchFront.pop_front();

      if(robotsTxtActive) {
        robotsTxtActive = false;

        searchFront.remove_if([&](const std::string &url) { return robotsTxt.matches(url); });
      }
    }

    void handleLine(const char *b, const char *e) {
      if(seenLines->contains(b, e - b)) return;

      reportDownloadedNew += e - b;
      seenLines->insert(b, e - b);
      output->handleLine(b, e);

      if(!remainingFetches) return;

      // searching for:
      // h?ref=['"]([^"']+)['"]
      //   src=['"]([^"']+)['"]
      // l?ink=['"]([^"']+)['"]
      //   0123   4       5   6

      for(const char *s = b; s < e - 7; ++s) {
        if(*reinterpret_cast<const uint32_t *>(s) != *reinterpret_cast<const uint32_t *>("ref=") &&
           *reinterpret_cast<const uint32_t *>(s) != *reinterpret_cast<const uint32_t *>("src=") &&
           *reinterpret_cast<const uint32_t *>(s) != *reinterpret_cast<const uint32_t *>("ink=")) continue;

        s += 4;

        if(*s != '\'' && *s != '"') continue;

        const char *urlBegin = s + 1;
        const char *urlEnd = urlBegin;

        while(urlEnd != e && *urlEnd != *s) ++urlEnd;
        if(urlEnd == e) return;
        if(urlEnd == urlBegin) return;
        if(!remainingFetches) return;

        handleUrl(urlBegin, urlEnd);
      }
    }

    void handleUrl(const char *b, const char *e) {
      assert(b != e);
      if(e - b > maximalUrlLength) return;

      std::string url(b, e);

      // std::cout << "URL found: " << url << std::endl;
      // std::cout << "relative to: " << searchFront.front() << std::endl;

      if(url.substr(0, 7) == "mailto:") return;
      if(url.substr(0, 11) == "javascript:") return;
      if(url.substr(0, 8) == "https://") return;
      if(url.substr(0, 7) == "http://") {
        if(url.length() < 8) return;
        if(url.substr(7, 7 + hostname.length()) != hostname) return;
        url = url.substr(7 + hostname.length());
      }

      size_t anchor = url.find('#');
      if(anchor != std::string::npos) url = url.substr(0, anchor);

      std::string base;

      if(*b == '/') {
        base = "/";
        url = url.substr(1);
      } else {
        base = searchFront.front();
        base.erase(base.rfind('/') + 1);
      }

      while(url.length() > 3 && url[0] == '.' && url[1] == '.' && url[2] == '/') {
        if(base != "/") base.erase(base.rfind('/', base.length() - 2) + 1);
        url = url.substr(3);
      }

      // std::cout << "base: " << base << std::endl;
      // std::cout << "absolute: " << base + url << std::endl;

      url = base + url;

      if(seenUrls->contains(url)) return;
      seenUrls->insert(url);

      if(ignoreList->matches(url)) return;

      searchFront.push_back(url);
      assert(remainingFetches > 0);
      --remainingFetches;
    }

    void handleRobotsTxtLine(const char *b, const char *e) {
      if(b == e) return;

      const char *c = b;
      while(c != e && *c++ != ':'); --c;
      if(*c != ':') return;

      if(*b == 'U' || *b == 'u') {
        // User-agent: *
        // User-agent: agent

        while(++c, c != e && *c == ' ');
        robotsTxtRelevant = c != e && *c == '*';
      } else if(robotsTxtRelevant && (*b == 'D' || *b == 'd')) {
        // Disallow: /path

        while(--e, *e == '\n' || *e == '\r');
        while(++c, *c == ' ' || *c == '\t');

        robotsTxt.insert(std::string(c, e + 1));
      }
    }

    Domain(const Domain &);
};

#endif
