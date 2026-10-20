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
#include "slice-config.h"
#include "slice-telemetry.h"
#include <cstdio>
#include <algorithm>

namespace sdnmec {

// MEC server with a single-core FIFO CPU model and a per-slice rate limiter
// (the XDP stage) in front of it.
class MecServerApp : public ns3::Application {
public:
  MecServerApp() : m_mecId(0), m_port(9000), m_socket(nullptr),
                   m_packetCount(0), m_busyUntil(0.0) {}

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
      uint32_t size = packet->GetSize();
      if (size == 0) break;

      double now = ns3::Simulator::Now().GetSeconds();

      // Device id is carried in the payload: "TASK:<deviceId>:SEQ..."
      uint8_t buf[32];
      uint32_t n = std::min<uint32_t>(size, 31);
      packet->CopyData(buf, n);
      buf[n] = 0;
      uint32_t dev = 0;
      if (std::sscanf(reinterpret_cast<char*>(buf), "TASK:%u:", &dev) != 1) dev = 0;
      uint32_t slice = SliceOfDevice(dev);

      SliceTelemetry& tel = SliceTelemetry::Get();
      SliceStats& st = tel.At(m_mecId, slice);

      // Stage 1: per-slice rate limiter (XDP equivalent)
      if (!tel.Admit(m_mecId, slice, size, now)) {
        MetricsCollector::Get().RecordDeviceLoss(dev);
        continue;
      }

      // Stage 2: CPU (single FIFO server)
      double service = kBaseServiceSec + kPerByteServiceSec * size;
      double backlog = std::max(0.0, m_busyUntil - now);
      if (backlog > kMaxBacklogSec) {
        st.cpuDrops++;
        MetricsCollector::Get().RecordDeviceLoss(dev);
        continue;
      }
      m_busyUntil = now + backlog + service;
      st.busySec += service;
      st.pkts++;
      st.bytes += size;

      // Delay here = CPU queueing + service (network delay not included)
      MetricsCollector::Get().RecordDeviceReceive(dev, (backlog + service) * 1000.0);
      MetricsCollector::Get().RecordMecReceive(m_mecId, size, ns3::Simulator::Now());

      m_packetCount++;
      if (m_packetCount % 100 == 0) {
        std::cout << "[MEC-" << m_mecId << " @ " << now
                  << "s] Processed " << m_packetCount << " tasks" << std::endl;
      }
    }
  }

  uint32_t m_mecId;
  uint16_t m_port;
  ns3::Ptr<ns3::Socket> m_socket;
  uint32_t m_packetCount;
  double m_busyUntil;
};

} // namespace sdnmec

#endif
