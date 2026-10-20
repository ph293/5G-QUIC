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

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;
using namespace sdnmec;

NS_LOG_COMPONENT_DEFINE("SDNMECCompare");

int main(int argc, char* argv[])
{
  double duration = 25.0;
  std::string modeStr = "mm";          // none | pid | ff | mmraw | mm | mmsnn | mmsnnu
  uint32_t run = 1;                    // random seed number
  double burstInterval = 0.01;         // flooder sends one task every this many seconds
  std::string shape = "step";          // step | ramp | periodic
  uint32_t delay = 0;                  // control decisions take effect after this many seconds
  double cpuTarget = 0.70;
  std::string out = "results.csv";
  bool summary = true;

  CommandLine cmd;
  cmd.AddValue("duration", "Simulation duration (s)", duration);
  cmd.AddValue("mode", "Control mode: none | pid | ff | mmraw | mm | mmsnn | mmsnnu", modeStr);
  cmd.AddValue("shape", "Flood shape: step | ramp | periodic | spike | twin | saw", shape);
  cmd.AddValue("delay", "Control delay in intervals", delay);
  cmd.AddValue("run", "Random seed number", run);
  cmd.AddValue("burstInterval", "Flooder send interval (s)", burstInterval);
  cmd.AddValue("cpuTarget", "Per-MEC CPU target", cpuTarget);
  cmd.AddValue("out", "CSV file the result row is appended to", out);
  cmd.AddValue("summary", "Print the full metrics report", summary);
  cmd.Parse(argc, argv);

  RngSeedManager::SetRun(run);

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

  // Noisy neighbor: device 0 (slice 0, MEC-0) floods between 5 s and 15 s.
  // Device 3 (slice 1) shares MEC-0 and is the victim.
  // Flood window. "spike" = a short step (5-8 s), "twin" = two bursts (5-16 s), "saw" = repeated ramps.
  double burstStart = 5.0, burstEnd = 15.0;
  std::string devShape = shape;
  if (shape == "spike") { devShape = "step"; burstEnd = 8.0; }
  if (shape == "twin")  { burstEnd = 16.0; }
  deviceApps[0]->SetBurst(burstStart, burstEnd, burstInterval, devShape);

  Ptr<CasicControllerApp> ctrl = CreateObject<CasicControllerApp>();
  ctrl->Setup(Seconds(1.0), cpuTarget, ParseMode(modeStr),
              "log_" + modeStr + ".csv", false, delay);
  topo.controller.Get(0)->AddApplication(ctrl);
  ctrl->SetStartTime(Seconds(1.0));
  ctrl->SetStopTime(Seconds(duration));

  Simulator::Stop(Seconds(duration + 0.5));
  Simulator::Run();

  // ---- collect the result row ----
  const DeviceMetrics* v = MetricsCollector::Get().GetDevice(3);   // victim
  const DeviceMetrics* f = MetricsCollector::Get().GetDevice(0);   // flooder
  if (v && f) {
    double vLoss = (v->packetsSent > 0) ? 100.0 * v->packetsLost / v->packetsSent : 0.0;
    double fLoss = (f->packetsSent > 0) ? 100.0 * f->packetsLost / f->packetsSent : 0.0;
    std::vector<double> d = v->sampleDelays;
    std::sort(d.begin(), d.end());
    double p95 = 0.0;
    if (!d.empty()) {
      size_t idx = static_cast<size_t>(std::ceil(0.95 * d.size()));
      p95 = d[(idx > 0 ? idx - 1 : 0)];
    }
    double overshoot = ctrl->GetOvershootArea();
    double mecOver = ctrl->GetMecOvershootArea();

    bool newFile = true;
    { std::ifstream chk(out.c_str()); newFile = !chk.good(); }
    std::ofstream o(out.c_str(), std::ios::app);
    if (newFile) {
      o << "mode,run,shape,delay,burstInterval,victim_loss_pct,victim_avg_delay_ms,"
           "victim_p95_delay_ms,flooder_loss_pct,flooder_served,slice_overshoot_s,mec_overshoot_s\n";
    }
    o << modeStr << "," << run << "," << shape << "," << delay << "," << burstInterval << ","
      << vLoss << "," << v->GetAvgDelay() << "," << p95 << "," << fLoss << ","
      << f->packetsReceived << "," << overshoot << "," << mecOver << "\n";
    std::cout << "RESULT mode=" << modeStr << " run=" << run
              << " victimLoss=" << vLoss << "% victimDelay=" << v->GetAvgDelay()
              << "ms p95=" << p95 << "ms overshoot=" << overshoot << std::endl;
  }

  if (summary) {
    MetricsCollector::Get().PrintFinalSummary();
    SliceTelemetry::Get().PrintSummary();
  }

  Simulator::Destroy();
  return 0;
}
