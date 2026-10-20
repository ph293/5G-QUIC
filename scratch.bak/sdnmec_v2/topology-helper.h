#ifndef SDNMEC_TOPOLOGY_HELPER_H
#define SDNMEC_TOPOLOGY_HELPER_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"

namespace sdnmec {

constexpr uint32_t NUM_DEVICES = 6;
constexpr uint32_t NUM_SWITCHES = 2;
constexpr uint32_t NUM_MEC_SERVERS = 3;

struct Topology {
  ns3::NodeContainer devices;
  ns3::NodeContainer switches;
  ns3::NodeContainer mecServers;
  ns3::NodeContainer controller;

  ns3::Ipv4InterfaceContainer devIfaces[NUM_DEVICES];
  ns3::Ipv4InterfaceContainer mecIfaces[NUM_MEC_SERVERS];
  ns3::Ipv4InterfaceContainer swIfaces[NUM_DEVICES + NUM_MEC_SERVERS + 1];
  ns3::Ipv4InterfaceContainer ctrlIfaces[2];

  ns3::Ipv4Address mecAddresses[NUM_MEC_SERVERS];
  ns3::Ipv4Address controllerAddress;
};

class TopologyHelper {
public:
  static Topology Create()
  {
    Topology t;

    t.devices.Create(NUM_DEVICES);
    t.switches.Create(NUM_SWITCHES);
    t.mecServers.Create(NUM_MEC_SERVERS);
    t.controller.Create(1);

    ns3::InternetStackHelper stack;
    stack.Install(t.devices);
    stack.Install(t.switches);
    stack.Install(t.mecServers);
    stack.Install(t.controller);

    ns3::PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", ns3::StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", ns3::StringValue("2ms"));

    ns3::Ipv4AddressHelper address;
    uint32_t subnet = 1;

    for (uint32_t i = 0; i < NUM_DEVICES; ++i) {
      std::ostringstream net;
      net << "10.1." << subnet << ".0";
      address.SetBase(net.str().c_str(), "255.255.255.0");
      ns3::NetDeviceContainer nd = p2p.Install(t.devices.Get(i), t.switches.Get(0));
      t.devIfaces[i] = address.Assign(nd);
      t.swIfaces[i] = t.devIfaces[i];
      ++subnet;
    }

    {
      std::ostringstream net;
      net << "10.2." << subnet << ".0";
      address.SetBase(net.str().c_str(), "255.255.255.0");
      ns3::NetDeviceContainer nd = p2p.Install(t.switches.Get(0), t.switches.Get(1));
      t.swIfaces[NUM_DEVICES] = address.Assign(nd);
      ++subnet;
    }

    for (uint32_t i = 0; i < NUM_MEC_SERVERS; ++i) {
      std::ostringstream net;
      net << "10.3." << subnet << ".0";
      address.SetBase(net.str().c_str(), "255.255.255.0");
      ns3::NetDeviceContainer nd = p2p.Install(t.switches.Get(1), t.mecServers.Get(i));
      t.mecIfaces[i] = address.Assign(nd);
      t.mecAddresses[i] = t.mecIfaces[i].GetAddress(1);
      ++subnet;
    }

    {
      std::ostringstream net;
      net << "10.4." << subnet << ".0";
      address.SetBase(net.str().c_str(), "255.255.255.0");
      ns3::NetDeviceContainer nd = p2p.Install(t.switches.Get(0), t.controller.Get(0));
      t.ctrlIfaces[0] = address.Assign(nd);
      t.controllerAddress = t.ctrlIfaces[0].GetAddress(1);
      ++subnet;
    }

    {
      std::ostringstream net;
      net << "10.4." << subnet << ".0";
      address.SetBase(net.str().c_str(), "255.255.255.0");
      ns3::NetDeviceContainer nd = p2p.Install(t.switches.Get(1), t.controller.Get(0));
      t.ctrlIfaces[1] = address.Assign(nd);
      ++subnet;
    }

    ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    return t;
  }
};

} // namespace sdnmec

#endif
