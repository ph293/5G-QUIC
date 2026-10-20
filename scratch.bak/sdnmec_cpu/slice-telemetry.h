#ifndef SDNMEC_SLICE_TELEMETRY_H
#define SDNMEC_SLICE_TELEMETRY_H

#include "slice-config.h"
#include <map>
#include <utility>
#include <algorithm>
#include <iostream>

namespace sdnmec {

// Per (MEC, slice) counters. Plays the role of the eBPF maps in the real testbed:
// written by the data path (MEC app), read by the controller; the controller writes
// `rate`, which the data path enforces (the XDP token bucket).
struct SliceStats {
  double   busySec      = 0.0;                          // CPU time consumed
  uint64_t pkts         = 0;                            // packets processed
  uint64_t bytes        = 0;
  uint64_t cpuDrops     = 0;                            // dropped: CPU queue full
  uint64_t limiterDrops = 0;                            // dropped: rate limiter
  double   rate         = kRateMaxBytes;                // B/s, set by controller
  double   tokens       = kRateMaxBytes * kBucketSec;
  double   lastRefill   = 0.0;
};

class SliceTelemetry {
public:
  static SliceTelemetry& Get()
  {
    static SliceTelemetry inst;
    return inst;
  }

  SliceStats& At(uint32_t mec, uint32_t slice) { return m_stats[std::make_pair(mec, slice)]; }
  std::map<std::pair<uint32_t, uint32_t>, SliceStats>& All() { return m_stats; }

  // Token-bucket admission (the XDP stage). Returns false if the packet is dropped.
  bool Admit(uint32_t mec, uint32_t slice, uint32_t bytes, double now)
  {
    SliceStats& s = At(mec, slice);
    double cap = std::max(kBucketMinB, s.rate * kBucketSec);
    s.tokens = std::min(cap, s.tokens + s.rate * (now - s.lastRefill));
    s.lastRefill = now;
    if (s.tokens < bytes) { s.limiterDrops++; return false; }
    s.tokens -= bytes;
    return true;
  }

  void PrintSummary()
  {
    std::cout << "\n=== PER-SLICE SUMMARY (MEC, slice) ===" << std::endl;
    for (auto& kv : m_stats) {
      const SliceStats& s = kv.second;
      std::cout << "MEC-" << kv.first.first << " slice-" << kv.first.second
                << " | processed=" << s.pkts
                << " | limiterDrops=" << s.limiterDrops
                << " | cpuDrops=" << s.cpuDrops
                << " | cpuBusy=" << s.busySec << "s"
                << " | finalRate=" << s.rate << " B/s" << std::endl;
    }
  }

private:
  SliceTelemetry() {}
  std::map<std::pair<uint32_t, uint32_t>, SliceStats> m_stats;
};

} // namespace sdnmec
#endif
