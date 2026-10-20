#ifndef SDNMEC_METRICS_COLLECTOR_H
#define SDNMEC_METRICS_COLLECTOR_H

#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include <map>
#include <vector>
#include <string>
#include <iomanip>

namespace sdnmec {

struct DeviceMetrics {
  uint64_t bytesSent;
  uint64_t packetsSent;
  uint64_t packetsReceived;
  double totalDelay;
  double maxDelay;
  uint64_t packetsLost;
  std::vector<double> sampleDelays;

  DeviceMetrics()
    : bytesSent(0), packetsSent(0), packetsReceived(0),
      totalDelay(0.0), maxDelay(0.0), packetsLost(0) {}

  double GetAvgDelay() const
  {
    if (packetsReceived == 0) return 0.0;
    return totalDelay / packetsReceived;
  }

  double GetPacketLossRate() const
  {
    if (packetsSent == 0) return 0.0;
    return static_cast<double>(packetsLost) / packetsSent;
  }

  double GetTransferRate(double duration) const
  {
    if (duration <= 0) return 0.0;
    return (bytesSent * 8.0) / (duration * 1000000.0);
  }
};

struct MecMetrics {
  uint64_t bytesReceived;
  uint64_t packetsReceived;
  double lastArrivalTime;

  MecMetrics() : bytesReceived(0), packetsReceived(0), lastArrivalTime(0.0) {}
};

class MetricsCollector {
public:
  static MetricsCollector& Get()
  {
    static MetricsCollector instance;
    return instance;
  }

  void RecordDeviceSend(uint32_t deviceId, uint32_t bytes, ns3::Time now)
  {
    m_deviceMetrics[deviceId].bytesSent += bytes;
    m_deviceMetrics[deviceId].packetsSent++;
  }

  void RecordDeviceReceive(uint32_t deviceId, double delay)
  {
    auto& m = m_deviceMetrics[deviceId];
    m.packetsReceived++;
    m.totalDelay += delay;
    m.sampleDelays.push_back(delay);
    if (delay > m.maxDelay) m.maxDelay = delay;
  }

  void RecordDeviceLoss(uint32_t deviceId)
  {
    m_deviceMetrics[deviceId].packetsLost++;
  }

  void RecordMecReceive(uint32_t mecId, uint32_t bytes, ns3::Time now)
  {
    m_mecMetrics[mecId].bytesReceived += bytes;
    m_mecMetrics[mecId].packetsReceived++;
    m_mecMetrics[mecId].lastArrivalTime = now.GetSeconds();
  }

  void LogSnapshot()
  {
    double t = ns3::Simulator::Now().GetSeconds();
    std::cout << "\n[METRICS @ " << t << "s] === SNAPSHOT ===" << std::endl;

    uint64_t totalBytes = 0;
    uint64_t totalPackets = 0;

    for (auto& kv : m_deviceMetrics) {
      auto& m = kv.second;
      totalBytes += m.bytesSent;
      totalPackets += m.packetsSent;
      std::cout << "[METRICS] Device " << kv.first
                << " | sent=" << m.packetsSent << " pkts"
                << " (" << m.bytesSent << " B)"
                << " | recv=" << m.packetsReceived
                << " | loss=" << m.packetsLost
                << " (" << (m.GetPacketLossRate() * 100) << "%)"
                << " | avgDelay=" << std::fixed << std::setprecision(2)
                << m.GetAvgDelay() << "ms"
                << std::endl;
    }

    for (auto& kv : m_mecMetrics) {
      std::cout << "[METRICS] MEC-" << kv.first
                << " | recv=" << kv.second.packetsReceived << " pkts"
                << " (" << kv.second.bytesReceived << " B)"
                << std::endl;
    }

    std::cout << "[METRICS] Aggregate: " << totalPackets << " pkts, "
              << totalBytes << " bytes" << std::endl;
  }

  void PrintFinalSummary()
  {
    double t = ns3::Simulator::Now().GetSeconds();
    std::cout << "\n=========================================" << std::endl;
    std::cout << "  FINAL METRICS REPORT @ " << t << "s" << std::endl;
    std::cout << "=========================================" << std::endl;

    uint64_t globalBytes = 0;
    uint64_t globalSent = 0;
    uint64_t globalRecv = 0;
    uint64_t globalLost = 0;
    double globalDelaySum = 0.0;
    uint64_t globalDelayCount = 0;

    for (auto& kv : m_deviceMetrics) {
      auto& m = kv.second;
      globalBytes += m.bytesSent;
      globalSent += m.packetsSent;
      globalRecv += m.packetsReceived;
      globalLost += m.packetsLost;
      globalDelaySum += m.totalDelay;
      globalDelayCount += m.packetsReceived;
    }

    std::cout << "Transfer Time: " << t << "s" << std::endl;
    std::cout << "Total Data Sent: " << globalBytes << " bytes"
              << " (" << (globalBytes / std::max(t, 1.0)) << " B/s)" << std::endl;
    std::cout << "Average Transfer Rate: "
              << std::fixed << std::setprecision(2)
              << (globalBytes * 8.0) / (t * 1000000.0)
              << " Mbps" << std::endl;
    std::cout << "Total Packets Sent: " << globalSent << std::endl;
    std::cout << "Total Packets Received: " << globalRecv << std::endl;
    std::cout << "Total Packets Lost: " << globalLost << std::endl;

    double lossRate = (globalSent > 0)
      ? (static_cast<double>(globalLost) / globalSent * 100.0) : 0.0;
    std::cout << "Packet Loss Ratio: " << std::fixed << std::setprecision(2)
              << lossRate << "%" << std::endl;

    double avgDelay = (globalDelayCount > 0)
      ? (globalDelaySum / globalDelayCount) : 0.0;
    std::cout << "Average Communication Delay: "
              << std::fixed << std::setprecision(2)
              << avgDelay << "ms" << std::endl;

    std::cout << "Bandwidth Utilization: "
              << std::fixed << std::setprecision(1)
              << (globalBytes * 8.0) / (t * 1e6 / 3 * 100)
              << "%" << std::endl;

    std::cout << "Per-Device Breakdown:" << std::endl;
    for (auto& kv : m_deviceMetrics) {
      auto& m = kv.second;
      std::cout << "  Device " << kv.first << ":"
                << " sent=" << m.packetsSent
                << " recv=" << m.packetsReceived
                << " loss=" << m.packetsLost
                << " rate=" << std::fixed << std::setprecision(2)
                << m.GetTransferRate(t) << " Mbps"
                << " delay=" << m.GetAvgDelay() << "ms"
                << std::endl;
    }

    std::cout << "MEC Server Load:" << std::endl;
    for (auto& kv : m_mecMetrics) {
      std::cout << "  MEC-" << kv.first << ": "
                << kv.second.packetsReceived << " packets ("
                << kv.second.bytesReceived << " bytes)"
                << std::endl;
    }

    std::cout << "=========================================\n" << std::endl;
  }

private:
  MetricsCollector() {}
  std::map<uint32_t, DeviceMetrics> m_deviceMetrics;
  std::map<uint32_t, MecMetrics> m_mecMetrics;
};

} // namespace sdnmec

#endif
