#ifndef DOMAINSTREAM_H
#define DOMAINSTREAM_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdexcept>

class DomainStream {
  public:
    DomainStream(const std::string &filename) {
      fd = open(filename.c_str(), O_CREAT | O_LARGEFILE | O_TRUNC | O_WRONLY, 0644);
      if(fd < 0) throw std::runtime_error("could not open " + filename + ": " + strerror(errno));

      outBufferFill = outBuffer;
    }

    ~DomainStream() {
      flush();
      close(fd);
    }

    void handleRequest(const std::string &hostname, const char *b, const char *e) {
      buffer("==== PnRaIMfLIPytQUqGtmbDfHOtyOfdPJSgawuCgSjvQKUOGJgOqgkrEgLGUQsAcqJD ====\nhttp://", 82);
      buffer(hostname.c_str(), hostname.length());
      buffer(b, e - b);
      buffer("\n", 1);
    }

    void handleLine(const char *b, const char *e) {
      buffer(b, e - b);
    }

  private:
    static const int BUFFER_SIZE = 1024 * 512;

    int fd;

    char outBuffer[BUFFER_SIZE];
    char *outBufferFill;

    void buffer(const char *s, int len) {
      buffer(s, s + len);
    }

    void buffer(const char *b, const char *e) {
      if(e - b > BUFFER_SIZE - (outBufferFill - outBuffer)) flush();

      if(e - b > BUFFER_SIZE) {
        while(b != e) {
          int len = write(fd, b, e - b);
          if(len <= 0) throw std::runtime_error("write failed in weird way, 2" + std::string(strerror(errno)));
          b += len;
        }
      } else {
        memcpy(outBufferFill, b, e - b);
        outBufferFill += e - b;
      }
    }

    void flush() {
      const char *pos = outBuffer;
      while(pos != outBufferFill) {
        int len = write(fd, pos, outBufferFill - pos);
        if(len <= 0) throw std::runtime_error("write failed in weird way, 3" + std::string(strerror(errno)));
        pos += len;
      }

      outBufferFill = outBuffer;
    }
};

#endif
