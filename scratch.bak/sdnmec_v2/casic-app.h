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
#include <string>
#include <utility>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace sdnmec {

// none  : no control
// pid   : reactive PID on measured CPU, fixed per-slice share (published baseline [7])
// ff    : demand-aware rate cap per slice share (uses measured offered load), no sharing of spare CPU
// mmraw : max-min (water-filling) CPU allocation from LAST interval's measured demand
// mm    : max-min allocation from FORECAST demand (Holt forecast, horizon = delay + 1)
// mmsnn : max-min allocation from the LRSNN forecast (spiking reservoir, Lagrangian-trained readout)
// mmsnnu: same spiking reservoir, readout trained WITHOUT the Lagrangian constraint (ablation)
enum class ControlMode { NONE, PID, FF, MMRAW, MM, MMSNN, MMSNNU };

inline ControlMode ParseMode(const std::string& s)
{
  if (s == "pid")   return ControlMode::PID;
  if (s == "ff")    return ControlMode::FF;
  if (s == "mmraw") return ControlMode::MMRAW;
  if (s == "mm")    return ControlMode::MM;
  if (s == "mmsnn") return ControlMode::MMSNN;
  if (s == "mmsnnu") return ControlMode::MMSNNU;
  return ControlMode::NONE;
}

class CasicControllerApp : public ns3::Application {
public:
  typedef std::pair<uint32_t, uint32_t> Key;

  CasicControllerApp()
    : m_interval(1.0), m_mecCpuTarget(0.70), m_gain(1.5), m_trimGain(0.5),
      m_mode(ControlMode::MM), m_verbose(false), m_running(false),
      m_cycle(0), m_delay(0), m_overshoot(0.0), m_mecOvershoot(0.0) {}

  // delayCycles: how many control intervals pass before a decision takes effect
  void Setup(ns3::Time interval, double mecCpuTarget, ControlMode mode,
             std::string csvFile, bool verbose, uint32_t delayCycles = 0)
  {
    m_interval = interval;
    m_mecCpuTarget = mecCpuTarget;
    m_mode = mode;
    m_csvFile = csvFile;
    m_verbose = verbose;
    m_delay = delayCycles;
  }

  double GetOvershootArea() const    { return m_overshoot; }      // slice CPU above its share
  double GetMecOvershootArea() const { return m_mecOvershoot; }   // total MEC CPU above target

  // Max-min fair (water-filling) split of capacity C among demands d.
  static void WaterFill(const std::vector<double>& d, double C, std::vector<double>& alloc)
  {
    size_t n = d.size();
    alloc.assign(n, 0.0);
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(),
              [&](size_t a, size_t b) { return d[a] < d[b]; });
    double rem = C;
    for (size_t j = 0; j < n; ++j) {
      size_t i = idx[j];
      double fair = rem / static_cast<double>(n - j);
      alloc[i] = std::min(d[i], fair);
      rem -= alloc[i];
    }
  }

private:
  struct Loop {
    PidController pid;
    HoltPredictor dem;
    LrsnnPredictor snn;     // Lagrangian-trained readout
    LrsnnPredictor snnu;    // unconstrained readout (ablation)
    double prevBusy, prevArrWork, prevArrBytes, rate;
    Loop() : pid(0.8, 0.0, 0.1, 1.0), snn(true), snnu(false), prevBusy(0.0), prevArrWork(0.0),
             prevArrBytes(0.0), rate(kRateMaxBytes) {}
  };
  struct Obs { double util, dem, obytes, fc, fcSnn, fcSnnU, share; };

  virtual void StartApplication()
  {
    m_running = true;
    m_cycle = 0;
    for (uint32_t d = 0; d < kNumDevs; ++d) {
      m_keys.insert(std::make_pair(InitialMecOfDevice(d), SliceOfDevice(d)));
    }
    m_csv.open(m_csvFile.c_str());
    m_csv << "time,mec,slice,cpu_util,demand_util,share,rate_Bps,limiter_drops,cpu_drops\n";
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

    // 1. Telemetry
    std::map<Key, Obs> obs;
    std::map<uint32_t, double> mecUtil;
    for (auto& key : m_keys) {
      SliceStats& st = tel.At(key.first, key.second);
      Loop& L = m_loops[key];
      Obs o;
      o.util   = (st.busySec  - L.prevBusy)    / dt;
      o.dem    = (st.arrWork  - L.prevArrWork)  / dt;
      o.obytes = (st.arrBytes - L.prevArrBytes) / dt;
      L.prevBusy = st.busySec;
      L.prevArrWork = st.arrWork;
      L.prevArrBytes = st.arrBytes;
      L.dem.Observe(o.dem);
      o.fc = L.dem.Forecast(static_cast<double>(m_delay) + 1.0);
      L.snn.Observe(o.dem);
      L.snnu.Observe(o.dem);
      o.fcSnn  = L.snn.Forecast(static_cast<double>(m_delay) + 1.0);
      o.fcSnnU = L.snnu.Forecast(static_cast<double>(m_delay) + 1.0);
      o.share = m_mecCpuTarget / std::max<uint32_t>(1, slicesOnMec[key.first]);
      m_overshoot += std::max(0.0, o.util - o.share) * dt;
      mecUtil[key.first] += o.util;
      obs[key] = o;
    }
    for (auto& mu : mecUtil) m_mecOvershoot += std::max(0.0, mu.second - m_mecCpuTarget) * dt;

    // 2. Decide new rates
    std::map<Key, double> newRates;
    if (m_mode == ControlMode::PID) {
      for (auto& key : m_keys) {
        Loop& L = m_loops[key];
        const Obs& o = obs[key];
        double u = L.pid.Step(o.util - o.share, dt);
        L.rate = std::min(kRateMaxBytes,
                 std::max(kRateMinBytes, L.rate * std::exp(-m_gain * u)));
        newRates[key] = L.rate;
      }
    } else if (m_mode == ControlMode::FF) {
      for (auto& key : m_keys) {
        Loop& L = m_loops[key];
        const Obs& o = obs[key];
        double u = L.pid.Step(o.util - o.share, dt);
        double pd = std::max(1e-6, o.dem);
        double f  = std::min(1.0, o.share / pd);
        double base = (f >= 1.0) ? kRateMaxBytes : std::max(kRateMinBytes, f * o.obytes);
        newRates[key] = std::min(kRateMaxBytes,
                        std::max(kRateMinBytes, base * std::exp(-m_trimGain * u)));
      }
    } else if (m_mode == ControlMode::MMRAW || m_mode == ControlMode::MM ||
               m_mode == ControlMode::MMSNN || m_mode == ControlMode::MMSNNU) {
      for (auto& mu : mecUtil) {
        uint32_t mec = mu.first;
        std::vector<Key> ks;
        std::vector<double> dUse;
        for (auto& key : m_keys) {
          if (key.first != mec) continue;
          ks.push_back(key);
          double dv = obs[key].dem;                                   // MMRAW: measured demand
          if (m_mode == ControlMode::MM)     dv = obs[key].fc;       // Holt forecast
          if (m_mode == ControlMode::MMSNN)  dv = obs[key].fcSnn;    // LRSNN forecast
          if (m_mode == ControlMode::MMSNNU) dv = obs[key].fcSnnU;   // unconstrained spiking forecast
          dUse.push_back(dv);
        }
        std::vector<double> alloc;
        WaterFill(dUse, m_mecCpuTarget, alloc);
        for (size_t i = 0; i < ks.size(); ++i) {
          Loop& L = m_loops[ks[i]];
          const Obs& o = obs[ks[i]];
          double u = L.pid.Step(o.util - alloc[i], dt);
          double base = (dUse[i] <= alloc[i] + 1e-9 || o.dem < 1e-6)
                          ? kRateMaxBytes
                          : std::max(kRateMinBytes, alloc[i] * (o.obytes / o.dem));
          newRates[ks[i]] = std::min(kRateMaxBytes,
                            std::max(kRateMinBytes, base * std::exp(-m_trimGain * u)));
        }
      }
    }
    if (m_mode != ControlMode::NONE) {
      m_pending.push_back(std::make_pair(m_cycle + m_delay, newRates));
    }

    // 3. Apply decisions that are due (after the configured delay)
    for (auto it = m_pending.begin(); it != m_pending.end(); ) {
      if (it->first <= m_cycle) {
        for (auto& kv : it->second) tel.At(kv.first.first, kv.first.second).rate = kv.second;
        it = m_pending.erase(it);
      } else {
        ++it;
      }
    }

    // 4. Log
    for (auto& key : m_keys) {
      SliceStats& st = tel.At(key.first, key.second);
      const Obs& o = obs[key];
      m_csv << now << "," << key.first << "," << key.second << "," << o.util << ","
            << o.dem << "," << o.share << "," << st.rate << "," << st.limiterDrops << ","
            << st.cpuDrops << "\n";
      if (m_verbose && (o.util > o.share || st.rate < kRateMaxBytes)) {
        std::cout << "[CASIC @ " << now << "s] MEC-" << key.first << " slice-" << key.second
                  << " cpu=" << o.util << " demand=" << o.dem << " rate=" << st.rate
                  << " B/s" << std::endl;
      }
    }

    ns3::Simulator::Schedule(m_interval, &CasicControllerApp::ControlCycle, this);
  }

  ns3::Time m_interval;
  double m_mecCpuTarget, m_gain, m_trimGain;
  ControlMode m_mode;
  bool m_verbose, m_running;
  uint32_t m_cycle, m_delay;
  double m_overshoot, m_mecOvershoot;
  std::string m_csvFile;
  std::ofstream m_csv;
  std::set<Key> m_keys;
  std::map<Key, Loop> m_loops;
  std::vector<std::pair<uint32_t, std::map<Key, double> > > m_pending;
};

} // namespace sdnmec

#endif
