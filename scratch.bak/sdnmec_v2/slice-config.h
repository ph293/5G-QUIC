#ifndef SDNMEC_SLICE_CONFIG_H
#define SDNMEC_SLICE_CONFIG_H

#include <cstdint>

namespace sdnmec {

// Topology (must match TopologyHelper: 6 devices, 3 MEC servers)
constexpr uint32_t kNumDevs   = 6;
constexpr uint32_t kNumMecs   = 3;
constexpr uint32_t kNumSlices = 3;

// Tenant slice of a device: dev0,1 -> slice0 | dev2,3 -> slice1 | dev4,5 -> slice2
inline uint32_t SliceOfDevice(uint32_t d) { return d / 2; }

// Matches Industry5DeviceApp's initial assignment (deviceId % numMec).
// Result: every MEC serves two different slices, so a noisy slice can hurt a neighbor.
inline uint32_t InitialMecOfDevice(uint32_t d) { return d % kNumMecs; }

// ---- CPU model of a MEC server (single core, FIFO) ----
constexpr double kBaseServiceSec    = 0.010;    // fixed cost per packet (s)
constexpr double kPerByteServiceSec = 1.0e-5;   // extra cost per byte (s)
constexpr double kMaxBacklogSec     = 0.25;     // CPU queue limit -> drop beyond this

// ---- XDP-style per-slice token bucket ----
constexpr double kRateMaxBytes = 100e3;         // "unlimited" rate (B/s)
constexpr double kRateMinBytes = 2e3;           // floor, avoids starving a slice
constexpr double kBucketSec    = 0.2;           // bucket depth in seconds of traffic
constexpr double kBucketMinB   = 2048.0;        // bucket must fit the largest packet

} // namespace sdnmec
#endif
