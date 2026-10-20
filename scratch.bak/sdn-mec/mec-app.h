#ifndef SDNMEC_MEC_APP_H
#define SDNMEC_MEC_APP_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "metrics-collector.h"

namespace sdnmec {

class MecServerApp : public ns3::Application {
public:
  MecServerApp() : m_mecId(0), m_socket(nullptr), m_packetCount(0) {}

  void Setup(uint32_t mecId, uint16_t port = 9000)
  {
    m_mecId = mecId;
    m_port = port;
  }

private:
  virtual void StartApplication()
  {
    m_socket = ns3::Socket::CreateSocket(GetNode(),
      ns3::UdpSocketFactory::GetTypeId());
    m_socket->Bind(ns3::InetSocketAddress(ns3::Ipv4Address::GetAny(), m_port));
    m_socket->SetRecvCallback(
      ns3::MakeCallback(&MecServerApp::HandleReceive, this));

    std::cout << "[MEC-" << m_mecId << " @ "
              << ns3::Simulator::Now().GetSeconds()
              << "s] Server ready on port " << m_port << std::endl;
  }

  virtual void StopApplication()
  {
    if (m_socket) {
      m_socket->Close();
      m_socket = nullptr;
    }
  }

  void HandleReceive(ns3::Ptr<ns3::Socket> socket)
  {
    ns3::Ptr<ns3::Packet> packet;
    ns3::Address from;

    while ((packet = socket->RecvFrom(from)))
    {
      if (packet->GetSize() == 0) break;

      m_packetCount++;
      uint32_t packetSize = packet->GetSize();

      ns3::InetSocketAddress addr = ns3::InetSocketAddress::ConvertFrom(from);

      MetricsCollector::Get().RecordMecReceive(m_mecId, packetSize,
        ns3::Simulator::Now());

      if (m_packetCount % 20 == 0) {
        std::cout << "[MEC-" << m_mecId << " @ "
                  << ns3::Simulator::Now().GetSeconds()
                  << "s] Received " << m_packetCount << " tasks"
                  << " (latest: " << packetSize << "B from "
                  << addr.GetIpv4() << ")" << std::endl;
      }
    }
  }

  uint32_t m_mecId;
  uint16_t m_port;
  ns3::Ptr<ns3::Socket> m_socket;
  uint32_t m_packetCount;
};

} // namespace sdnmec

#endif
