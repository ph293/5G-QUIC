#ifndef SDNMEC_DEVICE_APP_H
#define SDNMEC_DEVICE_APP_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/random-variable-stream.h"
#include "metrics-collector.h"
#include <vector>

namespace sdnmec {

namespace {
  ns3::Ptr<ns3::UniformRandomVariable> s_drng = ns3::CreateObject<ns3::UniformRandomVariable>();
}

class Industry5DeviceApp : public ns3::Application {
public:
  Industry5DeviceApp()
    : m_deviceId(0), m_socket(nullptr), m_taskInterval(0.5),
      m_packetSize(1024), m_running(false),
      m_burstStart(0.0), m_burstEnd(0.0), m_burstInterval(0.0) {}

  void Setup(uint32_t deviceId,
             std::vector<ns3::Ipv4Address> mecAddresses,
             ns3::Time taskInterval = ns3::Seconds(0.5),
             uint32_t packetSize = 1024)
  {
    m_deviceId = deviceId;
    m_mecAddresses = mecAddresses;
    m_taskInterval = taskInterval;
    m_packetSize = packetSize;
  }

  // NEW: turn this device into a "noisy neighbor" between start and end (seconds),
  // sending one task every `interval` seconds.
  void SetBurst(double start, double end, double interval)
  {
    m_burstStart = start;
    m_burstEnd = end;
    m_burstInterval = interval;
  }

  void SetMecAssignment(uint32_t mecIndex)
  {
    m_assignedMec = mecIndex;
    std::cout << "[DEVICE " << m_deviceId << " @ "
              << ns3::Simulator::Now().GetSeconds()
              << "s] Re-assigned to MEC-" << mecIndex
              << " (" << m_mecAddresses[mecIndex] << ")" << std::endl;
  }

private:
  virtual void StartApplication()
  {
    m_running = true;
    m_assignedMec = m_deviceId % m_mecAddresses.size();

    m_socket = ns3::Socket::CreateSocket(GetNode(),
      ns3::UdpSocketFactory::GetTypeId());
    m_socket->Bind();
    m_socket->Connect(ns3::InetSocketAddress(
      m_mecAddresses[m_assignedMec], 9000));

    std::cout << "[DEVICE " << m_deviceId << " @ "
              << ns3::Simulator::Now().GetSeconds()
              << "s] Started. Initial target: MEC-" << m_assignedMec
              << " (" << m_mecAddresses[m_assignedMec] << ")"
              << std::endl;

    m_pktCounter = 0;
    ns3::Simulator::Schedule(ns3::Seconds(1.0) + ns3::Seconds(m_deviceId * 0.1),
                             &Industry5DeviceApp::GenerateTask, this);
  }

  virtual void StopApplication()
  {
    m_running = false;
    if (m_socket) {
      m_socket->Close();
      m_socket = nullptr;
    }
  }

  void GenerateTask()
  {
    if (!m_running) return;

    m_socket->Connect(ns3::InetSocketAddress(
      m_mecAddresses[m_assignedMec], 9000));

    ns3::Time now = ns3::Simulator::Now();
    std::string data = "TASK:" + std::to_string(m_deviceId)
      + ":SEQ" + std::to_string(m_pktCounter);
    data.resize(std::max(data.size(), size_t(m_packetSize)), '#');

    ns3::Ptr<ns3::Packet> packet = ns3::Create<ns3::Packet>(
      reinterpret_cast<const uint8_t*>(data.c_str()),
      m_packetSize);
    m_socket->Send(packet);
    MetricsCollector::Get().RecordDeviceSend(m_deviceId, m_packetSize, now);

    m_pktCounter++;
    if (m_pktCounter % 100 == 0) {
      std::cout << "[DEVICE " << m_deviceId << " @ "
                << ns3::Simulator::Now().GetSeconds()
                << "s] Sent " << m_pktCounter << " tasks to MEC-"
                << m_assignedMec << std::endl;
    }

    // NEW: burst mode (noisy neighbor)
    double t = now.GetSeconds();
    bool bursting = (m_burstInterval > 0.0 && t >= m_burstStart && t < m_burstEnd);
    ns3::Time base = bursting ? ns3::Seconds(m_burstInterval) : m_taskInterval;
    double jitter = bursting ? s_drng->GetValue(-0.2, 0.2) * m_burstInterval
                             : s_drng->GetValue(-0.1, 0.1);
    ns3::Time nextInterval = base + ns3::Seconds(jitter);
    if (nextInterval < ns3::MilliSeconds(1)) nextInterval = ns3::MilliSeconds(1);
    ns3::Simulator::Schedule(nextInterval,
      &Industry5DeviceApp::GenerateTask, this);
  }

  uint32_t m_deviceId;
  ns3::Ptr<ns3::Socket> m_socket;
  std::vector<ns3::Ipv4Address> m_mecAddresses;
  ns3::Time m_taskInterval;
  uint32_t m_packetSize;
  uint32_t m_assignedMec;
  uint32_t m_pktCounter;
  bool m_running;
  double m_burstStart, m_burstEnd, m_burstInterval;
};

} // namespace sdnmec

#endif
