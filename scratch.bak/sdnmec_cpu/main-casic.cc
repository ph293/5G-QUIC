#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"

#include "topology-helper.h"
#include "device-app.h"
#include "mec-app.h"
#include "casic-app.h"
#include "slice-telemetry.h"
#include "metrics-collector.h"

#include <iostream>

using namespace ns3;
using namespace sdnmec;

NS_LOG_COMPONENT_DEFINE("SDNMECCasicMain");

int main(int argc, char* argv[])
{
  double duration = 25.0;
  bool casic = true;              // false = baseline without control
  double cpuTarget = 0.70;        // per-MEC CPU target
  double burstStart = 5.0, burstEnd = 15.0, burstInterval = 0.01;

  CommandLine cmd;
  cmd.AddValue("duration", "Simulation duration in seconds", duration);
  cmd.AddValue("casic", "Enable CPU-aware control (false = baseline)", casic);
  cmd.AddValue("cpuTarget", "Per-MEC CPU utilisation target", cpuTarget);
  cmd.AddValue("burstInterval", "Noisy neighbor send interval (s)", burstInterval);
  cmd.Parse(argc, argv);

  std::cout << "\n=========================================" << std::endl;
  std::cout << "  CPU-aware SDN-MEC slice control (CASIC)" << std::endl;
  std::cout << "  control=" << (casic ? "ON" : "OFF") << std::endl;
  std::cout << "=========================================\n" << std::endl;

  Topology topo = TopologyHelper::Create();

  for (uint32_t i = 0; i < NUM_MEC_SERVERS; ++i) {
    Ptr<MecServerApp> mecApp = CreateObject<MecServerApp>();
    mecApp->Setup(i, 9000);
    topo.mecServers.Get(i)->AddApplication(mecApp);
    mecApp->SetStartTime(Seconds(0.5));
    mecApp->SetStopTime(Seconds(duration));
  }

  std::vector<Ptr<Industry5DeviceApp>> deviceApps;
  for (uint32_t i = 0; i < NUM_DEVICES; ++i) {
    std::vector<Ipv4Address> mecAddrs;
    for (uint32_t m = 0; m < NUM_MEC_SERVERS; ++m) {
      mecAddrs.push_back(topo.mecAddresses[m]);
    }
    Ptr<Industry5DeviceApp> devApp = CreateObject<Industry5DeviceApp>();
    double rate = 0.3 + (i % 3) * 0.15;
    devApp->Setup(i, mecAddrs, Seconds(rate), 512 + i * 128);
    topo.devices.Get(i)->AddApplication(devApp);
    devApp->SetStartTime(Seconds(1.0));
    devApp->SetStopTime(Seconds(duration));
    deviceApps.push_back(devApp);
  }

  // Noisy neighbor: device 0 (slice 0, on MEC-0) floods during [burstStart, burstEnd).
  // Device 3 (slice 1) shares MEC-0 and is the victim.
  deviceApps[0]->SetBurst(burstStart, burstEnd, burstInterval);

  Ptr<CasicControllerApp> ctrl = CreateObject<CasicControllerApp>();
  ctrl->Setup(Seconds(1.0), cpuTarget, casic,
              casic ? "casic_log.csv" : "baseline_log.csv");
  topo.controller.Get(0)->AddApplication(ctrl);
  ctrl->SetStartTime(Seconds(1.0));
  ctrl->SetStopTime(Seconds(duration));

  Simulator::Stop(Seconds(duration + 0.5));
  Simulator::Run();

  MetricsCollector::Get().PrintFinalSummary();
  SliceTelemetry::Get().PrintSummary();

  Simulator::Destroy();
  return 0;
}
