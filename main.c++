#include "Domain.h"

#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <iostream>
#include <map>
#include <cassert>
#include <adns.h>
#include <sys/epoll.h>

int main(int argc, char *argv[]) {
  std::vector<Domain *> domains, domainsNew, domainsResolving, domainsDownloading;

  if(argc != 2) {
    std::cerr << "usage: ./crawler <config>" << std::endl;
    return 1;
  }

  PostfixSet ignoreList;
  uint64_t expectedLines = 100000;
  uint64_t activeDomains = 1024;

  {
    std::map<std::string, Domain *> hostUnifier;
    uint64_t cooldownMilliseconds = 5000;
    uint64_t fetchesPerDomain = 1000;
    uint64_t recursionMode = 1;
    std::string outputPath = "data";

    std::ifstream config(argv[1]);

    std::string configKeyword;
    while(config.good()) {
      getline(config, configKeyword, ' ');
      if(configKeyword == "") break;

      if(configKeyword == "expectedLines") {
        config >> expectedLines; config.get();
      } else if(configKeyword == "cooldownMilliseconds") {
        config >> cooldownMilliseconds; config.get();
      } else if(configKeyword == "fetchesPerDomain") {
        config >> fetchesPerDomain; config.get();
      } else if(configKeyword == "activeDomains") {
        config >> activeDomains; config.get();
      } else if(configKeyword == "recursionMode") {
        config >> recursionMode; config.get();
      } else if(configKeyword == "outputPath") {
        std::string path;
        getline(config, path);
        outputPath = path;
      } else if(configKeyword == "ignore") {
        std::string extension;
        getline(config, extension);
        ignoreList.insert(extension);
      } else if(configKeyword == "fetch") {
        std::string domain;
        getline(config, domain);

        Domain *&d = hostUnifier[Domain::extractHost(domain)];
        if(!d) {
          domains.push_back(d = new Domain(domain));
          d->setRemainingFetches(fetchesPerDomain);
          d->setCooldownMilliseconds(cooldownMilliseconds);
          d->setRecursionMode(recursionMode);
          d->setOutputPath(outputPath);
        }

        d->fetch(domain);
      } else {
        std::cerr << "Unknow config keyword: " << configKeyword << std::endl;
        return 1;
      }
    }
  }

  BloomSet seenLines(expectedLines);

  for(auto d: domains) {
    d->setSeenLines(&seenLines);
    d->setIgnoreList(&ignoreList);
    domainsNew.push_back(d);
  }

  adns_state adnsState;
  adns_init(&adnsState, adns_initflags(), 0);

  int epollHandle = epoll_create(activeDomains);

  while(!domainsNew.empty() || !domainsResolving.empty() || !domainsDownloading.empty()) {
    int downloadingCount = 0;
    for(auto d: domainsDownloading) downloadingCount += !!d;

    std::cout << "\e[1;1H\e[2J";

    Domain::ReportSum sum = { 0 };
    for(size_t i = 0; i < domainsDownloading.size(); ++i) {
      if(!domainsDownloading[i]) continue;

      domainsDownloading[i]->report(&sum);
    }
    std::cout << "[" <<
      std::setw(10) << sum.reportDownloaded << " b/s | " <<
      std::setw(10) << sum.reportDownloadedNew << " b/s ], " <<
      std::setw(8) << sum.remainingFetches << " | " <<
      std::setw(8) << sum.searchFrontSize << " -- Totals"
      << std::endl;

    int bloomFill = seenLines.estimateFill();

    std::cout <<
      "Entering loop. New: " << domainsNew.size() <<
      ", Resolving: " << domainsResolving.size() <<
      ", Downloading: " << downloadingCount <<
      ", Bloomfilter fill (0 - 1000): " << bloomFill<<
      std::endl;

    if(bloomFill > 750) {
      std::cerr << "Bloom filter is full." << std::endl;
      break;
    }

    for(int i = 0; i < 10; ++i) {
      while(!domainsNew.empty() &&
          domainsResolving.size() + downloadingCount < activeDomains && !domainsNew.empty() &&
          domainsResolving.size() < 128) { // empirical testing says too many outstanding queries just timeout
        adns_query query;

        adns_submit(adnsState,
            domainsNew.back()->getHostname().c_str(), 
            adns_r_a, adns_queryflags(), domainsNew.back(), &query);

        domainsResolving.push_back(domainsNew.back());
        domainsNew.pop_back();
      }

      while(1) {
        Domain *resolved;
        adns_query query = 0;
        adns_answer *answer = 0;

        adns_check(adnsState, &query, &answer, reinterpret_cast<void **>(&resolved));

        if(!answer) break;

        auto pos = std::find(domainsResolving.begin(), domainsResolving.end(), resolved);

        if(answer->status != adns_s_ok) {
          std::cout << "Domain resolution failed (" << answer->status << ") for: " << resolved->getHostname() << std::endl;
        } else {
          (*pos)->setIp(answer->rrs.inaddr->s_addr);

          // std::cout << "Domain resolved: " << resolved->getHostname() << " -> " << resolved->getIpString() << std::endl;

          auto zero = find(domainsDownloading.begin(), domainsDownloading.end(), nullptr);
          if(zero == domainsDownloading.end()) {
            domainsDownloading.push_back(*pos);
            zero = domainsDownloading.end() - 1;
          } else {
            *zero = *pos;
          }

          (*pos)->startDownloading([&](int fd, bool in, bool out) {
            epoll_event ev { static_cast<uint32_t>(in * EPOLLIN | out * EPOLLOUT),
              { .u64 = static_cast<uint64_t>(zero - domainsDownloading.begin()) }};
            epoll_ctl(epollHandle, EPOLL_CTL_ADD, fd, &ev);
          });
        }

        assert(pos != domainsResolving.end());

        *pos = domainsResolving.back();
        domainsResolving.pop_back();
      }

      timeval end;
      gettimeofday(&end, 0);
      end.tv_usec += 100000;

      if(end.tv_usec >= 1000000) {
        end.tv_usec -= 1000000;
        end.tv_sec += 1;
      }

      while(1) {
        epoll_event epollEvent;

        timeval now;
        gettimeofday(&now, 0);
        int msRemaining = ((end.tv_sec - now.tv_sec) * 1000000 + end.tv_usec - now.tv_usec) / 1000;
        if(msRemaining < 0) break;
        if(epoll_wait(epollHandle, &epollEvent, 1, msRemaining) <= 0) break;

        assert(epollEvent.data.u64 < domainsDownloading.size());

        Domain *domain = domainsDownloading[epollEvent.data.u64];
        
        auto finish = [&] { domainsDownloading[epollEvent.data.u64] = 0; };
        auto _ = [&](int action) {
          return [&, action](int fd, bool in, bool out) {
            epoll_event ev { static_cast<uint32_t>(in * EPOLLIN | out * EPOLLOUT), { .u64 = epollEvent.data.u64 }};
            epoll_ctl(epollHandle, action, fd, &ev);
          };
        };

        if(epollEvent.events & EPOLLIN) domain->handleInput(_(EPOLL_CTL_ADD), _(EPOLL_CTL_MOD), _(EPOLL_CTL_DEL), finish);
        if(epollEvent.events & EPOLLOUT) domain->handleOutput(_(EPOLL_CTL_ADD), _(EPOLL_CTL_MOD), _(EPOLL_CTL_DEL), finish);
        if(epollEvent.events & (EPOLLERR | EPOLLHUP)) domain->handleError(_(EPOLL_CTL_ADD), _(EPOLL_CTL_MOD), _(EPOLL_CTL_DEL), finish);
      }
    }

    while(!domainsDownloading.empty() && !domainsDownloading.back()) domainsDownloading.pop_back();

    for(size_t i = 0; i < domainsDownloading.size(); ++i) {
      if(!domainsDownloading[i]) continue;

      auto finish = [&] { domainsDownloading[i] = 0; };
      auto _ = [&](int action) {
        return [&, action](int fd, bool in, bool out) {
          epoll_event ev { static_cast<uint32_t>(in * EPOLLIN | out * EPOLLOUT), { .u64 = i }};
          epoll_ctl(epollHandle, action, fd, &ev);
        };
      };

      domainsDownloading[i]->handleLoop(_(EPOLL_CTL_ADD), _(EPOLL_CTL_MOD), _(EPOLL_CTL_DEL), finish);
    }
  }

  close(epollHandle);
  adns_finish(adnsState);

  for(auto d: domains) {
    d->finishDownloading();
    delete d;
  }

  return 0;
}
