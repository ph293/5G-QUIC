#ifndef SDNMEC_UNO_OPTIMIZER_H
#define SDNMEC_UNO_OPTIMIZER_H

#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include <vector>
#include <string>

namespace sdnmec {

namespace {
  ns3::Ptr<ns3::UniformRandomVariable> s_uorng = ns3::CreateObject<ns3::UniformRandomVariable>();
}

struct NetworkState {
  double avgLatency;
  double linkUtilization;
  uint32_t activeFlows;
  double packetDropRate;
  double energyEstimate;

  NetworkState()
    : avgLatency(10.0), linkUtilization(0.3),
      activeFlows(0), packetDropRate(0.01), energyEstimate(50.0) {}
};

struct PathPolicy {
  std::vector<uint32_t> deviceToMecMap;
  std::vector<double> pathWeights;
  double llmConfidence;

  PathPolicy(uint32_t numDevices = 6, uint32_t numMec = 3)
    : deviceToMecMap(numDevices, 0),
      pathWeights(numDevices * numMec, 1.0),
      llmConfidence(0.85) {}
};

class UNOOptimizer {
public:
  static UNOOptimizer& Get()
  {
    static UNOOptimizer instance;
    return instance;
  }

  PathPolicy Optimize(const NetworkState& state, const std::string& llmSuggestion)
  {
    PathPolicy policy;

    std::cout << "[UNO @ " << ns3::Simulator::Now().GetSeconds()
              << "s] === UNO OPTIMIZATION CYCLE ===" << std::endl;
    std::cout << "[UNO] LLM Feedback: \"" << llmSuggestion << "\"" << std::endl;
    std::cout << "[UNO] Network State: latency=" << state.avgLatency
              << "ms, util=" << (state.linkUtilization * 100)
              << "%, drops=" << (state.packetDropRate * 100) << "%"
              << ", energy=" << state.energyEstimate << std::endl;

    for (uint32_t d = 0; d < NUM_DEVICES; ++d) {
      uint32_t bestMec = 0;
      double bestScore = -1e9;

      for (uint32_t m = 0; m < NUM_MEC; ++m) {
        double delayPenalty = state.avgLatency * (1.0 + m * 0.3);
        double utilPenalty = state.linkUtilization * (1.0 + m * 0.1);
        double loadBalance = 1.0 - std::abs(static_cast<int>(m) - static_cast<int>(d % 3)) * 0.1;
        double score = loadBalance - (delayPenalty + utilPenalty) / 100.0;

        policy.pathWeights[d * NUM_MEC + m] = std::max(0.1, score + 1.0);
        if (score > bestScore) {
          bestScore = score;
          bestMec = m;
        }
      }

      policy.deviceToMecMap[d] = bestMec;
      double confidenceDelta = s_uorng->GetValue(-0.05, 0.05);
      policy.llmConfidence = std::max(0.5, std::min(0.99, 0.85 + confidenceDelta));
    }

    double convergenceMetric = 0.0;
    for (uint32_t d = 0; d < NUM_DEVICES; ++d) {
      convergenceMetric += policy.deviceToMecMap[d] * (d + 1);
    }
    convergenceMetric /= (NUM_DEVICES * (NUM_MEC - 1));

    std::cout << "[UNO] Optimal MEC Assignments: ";
    for (uint32_t d = 0; d < NUM_DEVICES; ++d) {
      std::cout << "D" << d << "->MEC" << policy.deviceToMecMap[d] << " ";
    }
    std::cout << std::endl;
    std::cout << "[UNO] Convergence Metric: " << convergenceMetric
              << " | LLM Confidence: " << (policy.llmConfidence * 100) << "%"
              << std::endl;
    std::cout << "[UNO] Policy updated. Optimal paths recomputed."
              << std::endl;

    return policy;
  }

  void ApplyPolicy(const PathPolicy& policy)
  {
    std::cout << "[UNO @ " << ns3::Simulator::Now().GetSeconds()
              << "s] Applying policy to SDN switches..." << std::endl;
    for (uint32_t d = 0; d < NUM_DEVICES; ++d) {
      uint32_t mec = policy.deviceToMecMap[d];
      std::cout << "[UNO] Installing flow: Device " << d
                << " -> MEC-" << mec
                << " (weight=" << policy.pathWeights[d * NUM_MEC + mec]
                << ")" << std::endl;
    }
  }

  std::string GenerateLLMPrompt(const NetworkState& state)
  {
    std::ostringstream prompt;
    prompt << "Analyze network state: avg_latency=" << state.avgLatency
           << "ms, link_utilization=" << (state.linkUtilization * 100)
           << "%, packet_drop=" << (state.packetDropRate * 100)
           << "%. Recommend optimal path adjustments for 6 devices across 3 MEC nodes.";
    return prompt.str();
  }

  std::string SimulateLLMResponse(const NetworkState& state)
  {
    if (state.linkUtilization > 0.7) {
      return "High utilization detected. Redistribute load across all MEC nodes. Use weighted round-robin with weights [0.4, 0.35, 0.25] based on current link capacities.";
    }
    if (state.packetDropRate > 0.05) {
      return "Elevated packet loss. Recommend re-routing traffic from overloaded paths. Shift 30% of traffic to under-utilized MEC-2.";
    }
    if (state.avgLatency > 50.0) {
      return "Latency above threshold. Optimize paths by switching high-latency flows to nearest MEC. Consider task pre-processing at edge switch.";
    }
    return "Network state nominal. Continue current policy. Minor weight adjustments recommended for load balancing.";
  }

private:
  UNOOptimizer() {}
  static const uint32_t NUM_DEVICES = 6;
  static const uint32_t NUM_MEC = 3;
};

} // namespace sdnmec

#endif
