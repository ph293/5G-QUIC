#ifndef SDNMEC_CMBDQ_MODEL_H
#define SDNMEC_CMBDQ_MODEL_H

#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include <vector>
#include <map>
#include <algorithm>

namespace sdnmec {

namespace {
  ns3::Ptr<ns3::UniformRandomVariable> s_urng = ns3::CreateObject<ns3::UniformRandomVariable>();
  ns3::Ptr<ns3::GammaRandomVariable> s_grng = ns3::CreateObject<ns3::GammaRandomVariable>();
}

struct TrafficStats {
  double avgDelay;
  double packetLoss;
  double queueLength;
  double bandwidthUsage;

  TrafficStats()
    : avgDelay(0.0), packetLoss(0.0), queueLength(0.0), bandwidthUsage(0.0) {}
};

struct QValue {
  std::vector<double> values;
  QValue(uint32_t numActions = 3) : values(numActions, 0.5) {}
};

class CMBDQModel {
public:
  static CMBDQModel& Get()
  {
    static CMBDQModel instance;
    return instance;
  }

  uint32_t SelectMecServer(uint32_t deviceId, const TrafficStats& stats)
  {
    EnsureQTable(deviceId);

    double beta = SampleBeta(2.0, 5.0);
    double explorationProb = 0.2 * (1.0 - beta);

    std::vector<double>& q = m_qTable[deviceId].values;

    if (s_urng->GetValue(0, 1) < explorationProb) {
      uint32_t action = static_cast<uint32_t>(
        s_urng->GetValue(0, q.size()));
      std::cout << "[CMBDQ @ " << ns3::Simulator::Now().GetSeconds()
                << "s] Device " << deviceId
                << " EXPLORE: selecting MEC-" << action
                << " (beta=" << beta << ")" << std::endl;
      return action;
    }

    uint32_t best = std::distance(q.begin(),
      std::max_element(q.begin(), q.end()));

    UpdateQValues(deviceId, best, stats);
    std::cout << "[CMBDQ @ " << ns3::Simulator::Now().GetSeconds()
              << "s] Device " << deviceId
              << " EXPLOIT: selecting MEC-" << best
              << " (Q=[" << q[0] << "," << q[1] << "," << q[2] << "])"
              << std::endl;
    return best;
  }

  void UpdateFromFeedback(uint32_t deviceId, uint32_t mecIdx, double reward)
  {
    EnsureQTable(deviceId);
    std::vector<double>& q = m_qTable[deviceId].values;
    double lr = 0.15;
    q[mecIdx] = (1.0 - lr) * q[mecIdx] + lr * reward;
    std::cout << "[CMBDQ @ " << ns3::Simulator::Now().GetSeconds()
              << "s] Feedback: device=" << deviceId
              << " mec=" << mecIdx
              << " reward=" << reward
              << " newQ=" << q[mecIdx] << std::endl;
  }

  void PrintSummary()
  {
    std::cout << "[CMBDQ] Q-Table Summary:" << std::endl;
    for (auto& kv : m_qTable) {
      std::cout << "  Device " << kv.first << ": [";
      for (size_t i = 0; i < kv.second.values.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << kv.second.values[i];
      }
      std::cout << "]" << std::endl;
    }
  }

private:
  CMBDQModel() {}

  void EnsureQTable(uint32_t deviceId)
  {
    if (m_qTable.find(deviceId) == m_qTable.end()) {
      m_qTable[deviceId] = QValue(NUM_MEC);
      std::vector<double>& q = m_qTable[deviceId].values;
      for (uint32_t i = 0; i < q.size(); ++i) {
        q[i] = s_urng->GetValue(0.3, 0.7);
      }
    }
  }

  double SampleBeta(double a, double b)
  {
    double x = s_grng->GetValue(a, 1.0);
    double y = s_grng->GetValue(b, 1.0);
    return x / (x + y + 1e-10);
  }

  void UpdateQValues(uint32_t deviceId, uint32_t action, const TrafficStats& st)
  {
    double reward = 1.0 / (1.0 + st.avgDelay);
    reward -= 0.5 * st.packetLoss;
    reward += 0.3 * (1.0 - st.queueLength / 100.0);
    reward = std::max(0.01, std::min(1.0, reward));
    UpdateFromFeedback(deviceId, action, reward);
  }

  static const uint32_t NUM_MEC = 3;
  std::map<uint32_t, QValue> m_qTable;
};

} // namespace sdnmec

#endif
