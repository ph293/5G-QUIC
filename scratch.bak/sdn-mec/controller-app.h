#ifndef SDNMEC_CONTROLLER_APP_H
#define SDNMEC_CONTROLLER_APP_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "topology-helper.h"
#include "cmbdq-model.h"
#include "uno-optimizer.h"
#include "metrics-collector.h"

namespace sdnmec {

namespace {
  ns3::Ptr<ns3::UniformRandomVariable> s_crng = ns3::CreateObject<ns3::UniformRandomVariable>();
}

class SDNControllerApp : public ns3::Application {
public:
  SDNControllerApp()
    : m_cmdbqInterval(3.0), m_unoInterval(5.0),
      m_monitorInterval(2.0), m_running(false) {}

  void Setup(ns3::Time cmbdqInterval = ns3::Seconds(3.0),
             ns3::Time unoInterval = ns3::Seconds(5.0),
             ns3::Time monitorInterval = ns3::Seconds(2.0))
  {
    m_cmdbqInterval = cmbdqInterval;
    m_unoInterval = unoInterval;
    m_monitorInterval = monitorInterval;
  }

private:
  virtual void StartApplication()
  {
    m_running = true;
    m_cycle = 0;

    m_socket = ns3::Socket::CreateSocket(GetNode(),
      ns3::UdpSocketFactory::GetTypeId());
    m_socket->Bind(ns3::InetSocketAddress(ns3::Ipv4Address::GetAny(), 9999));

    std::cout << "\n[CTRL @ " << ns3::Simulator::Now().GetSeconds()
              << "s] SDN Controller started. Monitoring network..."
              << std::endl;

    ns3::Simulator::Schedule(ns3::Seconds(2.0),
      &SDNControllerApp::RunCMBDQCycle, this);
    ns3::Simulator::Schedule(ns3::Seconds(4.0),
      &SDNControllerApp::RunUNOCycle, this);
    ns3::Simulator::Schedule(m_monitorInterval,
      &SDNControllerApp::MonitorTraffic, this);
  }

  virtual void StopApplication()
  {
    m_running = false;
    if (m_socket) {
      m_socket->Close();
      m_socket = nullptr;
    }
  }

  void RunCMBDQCycle()
  {
    if (!m_running) return;
    m_cycle++;

    std::cout << "\n[CTRL @ " << ns3::Simulator::Now().GetSeconds()
              << "s] === CMBDQ CYCLE " << m_cycle << " ===" << std::endl;

    ns3::Time now = ns3::Simulator::Now();
    TrafficStats stats;
    stats.avgDelay = s_crng->GetValue(5.0, 30.0);
    stats.packetLoss = s_crng->GetValue(0.0, 0.08);
    stats.bandwidthUsage = s_crng->GetValue(0.2, 0.8);
    stats.queueLength = s_crng->GetValue(10.0, 90.0);

    std::cout << "[CTRL] Network snapshot: delay="
              << stats.avgDelay << "ms, loss="
              << (stats.packetLoss * 100) << "%, bw="
              << (stats.bandwidthUsage * 100) << "%, queue="
              << stats.queueLength << "%" << std::endl;

    for (uint32_t d = 0; d < NUM_DEVICES; ++d) {
      uint32_t mec = CMBDQModel::Get().SelectMecServer(d, stats);
      NotifyDeviceAssignment(d, mec);
    }

    CMBDQModel::Get().PrintSummary();

    ns3::Simulator::Schedule(m_cmdbqInterval,
      &SDNControllerApp::RunCMBDQCycle, this);
  }

  void RunUNOCycle()
  {
    if (!m_running) return;

    std::cout << "\n[CTRL @ " << ns3::Simulator::Now().GetSeconds()
              << "s] === UNO OPTIMIZATION CYCLE ===" << std::endl;

    NetworkState state;
    state.avgLatency = s_crng->GetValue(10.0, 60.0);
    state.linkUtilization = s_crng->GetValue(0.3, 0.85);
    state.activeFlows = NUM_DEVICES * 2;
    state.packetDropRate = s_crng->GetValue(0.0, 0.06);
    state.energyEstimate = s_crng->GetValue(40.0, 80.0);

    std::string prompt = UNOOptimizer::Get().GenerateLLMPrompt(state);
    std::cout << "[CTRL] LLM Prompt: \"" << prompt << "\"" << std::endl;

    std::string llmResponse = UNOOptimizer::Get().SimulateLLMResponse(state);

    PathPolicy policy = UNOOptimizer::Get().Optimize(state, llmResponse);
    UNOOptimizer::Get().ApplyPolicy(policy);

    ns3::Simulator::Schedule(m_unoInterval,
      &SDNControllerApp::RunUNOCycle, this);
  }

  void MonitorTraffic()
  {
    if (!m_running) return;

    double anomalyScore = s_crng->GetValue(0.0, 1.0);

    if (anomalyScore > 0.7) {
      std::cout << "[CTRL @ " << ns3::Simulator::Now().GetSeconds()
                << "s] WARNING: Traffic anomaly detected! Score="
                << anomalyScore << std::endl;

      std::string logSummary = "AGG_LOG: Spike in traffic at t=";
      logSummary += std::to_string(ns3::Simulator::Now().GetSeconds());
      logSummary += "s. 3 devices showing above-normal tx rates.";

      std::cout << "[CTRL] Logs aggregated for LLM analysis..." << std::endl;
      std::cout << "[CTRL] LLM Analysis: \"" << logSummary
                << "\"" << std::endl;
      std::cout << "[CTRL] Adaptive measures triggered."
                << std::endl;

      if (anomalyScore > 0.9) {
        std::cout << "[CTRL] CRITICAL: Initiating emergency path re-routing."
                  << std::endl;
        NetworkState emergencyState;
        emergencyState.avgLatency = 80.0;
        emergencyState.linkUtilization = 0.95;
        emergencyState.packetDropRate = 0.08;
        std::string critResponse = UNOOptimizer::Get().SimulateLLMResponse(emergencyState);
        PathPolicy emergencyPolicy = UNOOptimizer::Get().Optimize(emergencyState, critResponse);
        UNOOptimizer::Get().ApplyPolicy(emergencyPolicy);
      }
    } else {
      std::cout << "[CTRL @ " << ns3::Simulator::Now().GetSeconds()
                << "s] Traffic nominal (score=" << anomalyScore << ")"
                << std::endl;
    }

    MetricsCollector::Get().LogSnapshot();

    ns3::Simulator::Schedule(m_monitorInterval,
      &SDNControllerApp::MonitorTraffic, this);
  }

  void NotifyDeviceAssignment(uint32_t deviceId, uint32_t mecIndex)
  {
    std::cout << "[CTRL] Assignment: Device " << deviceId
              << " -> MEC-" << mecIndex << std::endl;
  }

  ns3::Time m_cmdbqInterval;
  ns3::Time m_unoInterval;
  ns3::Time m_monitorInterval;
  ns3::Ptr<ns3::Socket> m_socket;
  uint32_t m_cycle;
  bool m_running;

  static const uint32_t NUM_DEVICES = 6;
};

} // namespace sdnmec

#endif
