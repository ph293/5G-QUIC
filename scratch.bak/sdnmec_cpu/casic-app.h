#ifndef SDNMEC_CASIC_APP_H
#define SDNMEC_CASIC_APP_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/simulator.h"
#include "metrics-collector.h"
#include "slice-config.h"
#include "slice-telemetry.h"
#include "pid-controller.h"
#include "predictor.h"
#include <fstream>
#include <map>
#include <set>
#include <utility>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace sdnmec {

// CPU-aware Adaptive Slice Intelligence Controller (CASIC).
// Every control interval, for each (MEC, slice):
//   telemetry -> predictor -> blend with measured CPU -> PID -> new rate limit.
class CasicControllerApp : public ns3::Application {
public:
  CasicControllerApp()
    : m_interval(1.0), m_mecCpuTarget(0.70), m_alpha(0.5),
      m_gain(1.5), m_enabled(true), m_running(false), m_cycle(0) {}

  void Setup(ns3::Time interval = ns3::Seconds(1.0),
             double mecCpuTarget = 0.70,
             bool enabled = true,
             std::string csvFile = "casic_log.csv")
  {
    m_interval = interval;
    m_mecCpuTarget = mecCpuTarget;
    m_enabled = enabled;
    m_csvFile = csvFile;
  }

private:
  struct Loop {
    PidController pid;
    EwmaTrendPredictor pred;      // swap for an LRSNN-based CpuPredictor later
    double prevBusy;
    uint64_t prevPkts;
    double rate;
    Loop() : pid(0.8, 0.0, 0.1, 1.0), prevBusy(0.0), prevPkts(0),
             rate(kRateMaxBytes) {}
  };

  virtual void StartApplication()
  {
    m_running = true;
    m_cycle = 0;

    // Which (MEC, slice) pairs exist, from the initial device assignment
    for (uint32_t d = 0; d < kNumDevs; ++d) {
      m_keys.insert(std::make_pair(InitialMecOfDevice(d), SliceOfDevice(d)));
    }

    m_csv.open(m_csvFile.c_str());
    m_csv << "time,mec,slice,cpu_util,cpu_pred,share,error,rate_Bps,limiter_drops,cpu_drops\n";

    std::cout << "\n[CASIC @ " << ns3::Simulator::Now().GetSeconds()
              << "s] Started. interval=" << m_interval.GetSeconds()
              << "s, MEC CPU target=" << m_mecCpuTarget
              << ", control " << (m_enabled ? "ON" : "OFF (baseline)") << std::endl;

    ns3::Simulator::Schedule(m_interval, &CasicControllerApp::ControlCycle, this);
  }

  virtual void StopApplication()
  {
    m_running = false;
    if (m_csv.is_open()) m_csv.close();
  }

  void ControlCycle()
  {
    if (!m_running) return;
    m_cycle++;

    double now = ns3::Simulator::Now().GetSeconds();
    double dt = m_interval.GetSeconds();
    SliceTelemetry& tel = SliceTelemetry::Get();

    std::map<uint32_t, uint32_t> slicesOnMec;
    for (auto& k : m_keys) slicesOnMec[k.first]++;

    for (auto& key : m_keys) {
      uint32_t mec = key.first, slice = key.second;
      SliceStats& st = tel.At(mec, slice);
      Loop& L = m_loops[key];

      // 1. Telemetry for this interval
      double util = (st.busySec - L.prevBusy) / dt;
      double pps  = static_cast<double>(st.pkts - L.prevPkts) / dt;
      L.prevBusy = st.busySec;
      L.prevPkts = st.pkts;

      // 2. Predict, then blend predicted and actual
      L.pred.Observe(util, pps);
      double pred   = L.pred.Forecast();
      double signal = m_alpha * pred + (1.0 - m_alpha) * util;

      // 3. Per-slice CPU share on this MEC
      double share = m_mecCpuTarget / std::max<uint32_t>(1, slicesOnMec[mec]);
      double err   = signal - share;

      // 4. PID -> rate adjustment (log domain); positive error means throttle
      double u = L.pid.Step(err, dt);
      if (m_enabled) {
        L.rate = std::min(kRateMaxBytes,
                 std::max(kRateMinBytes, L.rate * std::exp(-m_gain * u)));
        st.rate = L.rate;       // enforced by the MEC data path
      }

      m_csv << now << "," << mec << "," << slice << "," << util << ","
            << pred << "," << share << "," << err << "," << st.rate << ","
            << st.limiterDrops << "," << st.cpuDrops << "\n";

      if (util > share || st.rate < kRateMaxBytes) {
        std::cout << "[CASIC @ " << now << "s] MEC-" << mec << " slice-" << slice
                  << " cpu=" << util << " pred=" << pred << " share=" << share
                  << " -> rate=" << st.rate << " B/s" << std::endl;
      }
    }

    if (m_cycle % 2 == 0) MetricsCollector::Get().LogSnapshot();

    ns3::Simulator::Schedule(m_interval, &CasicControllerApp::ControlCycle, this);
  }

  ns3::Time m_interval;
  double m_mecCpuTarget;
  double m_alpha;
  double m_gain;
  bool m_enabled;
  bool m_running;
  uint32_t m_cycle;
  std::string m_csvFile;
  std::ofstream m_csv;
  std::set<std::pair<uint32_t, uint32_t>> m_keys;
  std::map<std::pair<uint32_t, uint32_t>, Loop> m_loops;
};

} // namespace sdnmec

#endif
