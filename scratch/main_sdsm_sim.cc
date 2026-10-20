#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"

#include "quic_sdsm.h"
#include "mcvt_snmo_sdn.h"

using namespace ns3;

std::ofstream g_metricsCsv;

int main(int argc, char *argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    g_metricsCsv.open("../../results/csv_data/simulation_metrics.csv");
    if (g_metricsCsv.is_open()) {
        g_metricsCsv << "Time_s,PacketSeq,Latency_ms,Throughput_Mbps,Controller_Load,Receiving_Server\n";
    }

    std::cout << "====================================================================" << std::endl;
    std::cout << "  QUIC SDSM 5G MEC - 5 Towers Complete Run (25 Seconds)             " << std::endl;
    std::cout << "====================================================================" << std::endl;

    // 1. Create Nodes
    NodeContainer ueNode;
    ueNode.Create(1);

    NodeContainer gnbNodes;
    gnbNodes.Create(5);

    NodeContainer mecNodes;
    mecNodes.Create(5);

    // 2. Setup Coordinates (Mobility)
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> staticPos = CreateObject<ListPositionAllocator>();
    staticPos->Add(Vector(10.0, 0.0, 10.0));   // Tower 1
    staticPos->Add(Vector(50.0, 0.0, 10.0));   // Tower 2
    staticPos->Add(Vector(100.0, 0.0, 10.0));  // Tower 3
    staticPos->Add(Vector(150.0, 0.0, 10.0));  // Tower 4
    staticPos->Add(Vector(190.0, 0.0, 10.0));  // Tower 5

    staticPos->Add(Vector(10.0, 20.0, 0.0));   // MEC 1
    staticPos->Add(Vector(50.0, 20.0, 0.0));   // MEC 2
    staticPos->Add(Vector(100.0, 20.0, 0.0));  // MEC 3
    staticPos->Add(Vector(150.0, 20.0, 0.0));  // MEC 4
    staticPos->Add(Vector(190.0, 20.0, 0.0));  // MEC 5

    mobility.SetPositionAllocator(staticPos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(gnbNodes);
    mobility.Install(mecNodes);

    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(ueNode);
    Ptr<ConstantVelocityMobilityModel> ueMob = ueNode.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    ueMob->SetPosition(Vector(0.0, 0.0, 1.5));
    ueMob->SetVelocity(Vector(8.0, 0.0, 0.0)); // 8 m/s

    // 3. Install Internet Stack
    InternetStackHelper internet;
    internet.Install(ueNode);
    internet.Install(gnbNodes);
    internet.Install(mecNodes);

    // 4. Create Links
    PointToPointHelper p2pEdge;
    p2pEdge.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    p2pEdge.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointHelper p2pWireless;
    p2pWireless.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2pWireless.SetChannelAttribute("Delay", StringValue("2ms"));

    std::vector<NetDeviceContainer> ueGnbLinks(5);
    std::vector<NetDeviceContainer> gnbMecLinks(5);
    for (int i = 0; i < 5; ++i) {
        ueGnbLinks[i] = p2pWireless.Install(ueNode.Get(0), gnbNodes.Get(i));
        gnbMecLinks[i] = p2pEdge.Install(gnbNodes.Get(i), mecNodes.Get(i));
    }

    for (int i = 0; i < 4; ++i) {
        p2pEdge.Install(mecNodes.Get(i), mecNodes.Get(i + 1));
    }

    // 5. Assign IP Subnets
    Ipv4AddressHelper ipv4;
    std::vector<Ipv4InterfaceContainer> ifGnbMec(5);

    for (int i = 0; i < 5; ++i) {
        std::string ueSubnet = "10.1." + std::to_string(i + 1) + ".0";
        ipv4.SetBase(ueSubnet.c_str(), "255.255.255.0");
        ipv4.Assign(ueGnbLinks[i]);

        std::string mecSubnet = "10.2." + std::to_string(i + 1) + ".0";
        ipv4.SetBase(mecSubnet.c_str(), "255.255.255.0");
        ifGnbMec[i] = ipv4.Assign(gnbMecLinks[i]);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // 6. Install 5 MEC Servers
    for (int i = 0; i < 5; ++i) {
        Ptr<QuicMecServer> server = CreateObject<QuicMecServer>();
        std::string name = "MEC Server " + std::to_string(i + 1);
        server->Setup(8080, name);
        mecNodes.Get(i)->AddApplication(server);
        server->SetStartTime(Seconds(0.5));
        server->SetStopTime(Seconds(25.0));
    }

    // 7. Install QUIC Client on the Car
    Ptr<QuicClientApp> clientApp = CreateObject<QuicClientApp>();
    clientApp->Setup(ifGnbMec[0].GetAddress(1), 8080);
    ueNode.Get(0)->AddApplication(clientApp);
    clientApp->SetStartTime(Seconds(1.0));
    clientApp->SetStopTime(Seconds(25.0));

    // 8. Schedule Handovers Across All 5 Towers
    Simulator::Schedule(Seconds(5.0), &TriggerHandoverAndMigration, clientApp, ifGnbMec[1].GetAddress(1), 2, "MEC Server 2");
    Simulator::Schedule(Seconds(10.0), &TriggerHandoverAndMigration, clientApp, ifGnbMec[2].GetAddress(1), 3, "MEC Server 3");
    Simulator::Schedule(Seconds(15.0), &TriggerHandoverAndMigration, clientApp, ifGnbMec[3].GetAddress(1), 4, "MEC Server 4");
    Simulator::Schedule(Seconds(20.0), &TriggerHandoverAndMigration, clientApp, ifGnbMec[4].GetAddress(1), 5, "MEC Server 5");

    // 9. Run Simulation
    Simulator::Stop(Seconds(25.0));
    Simulator::Run();
    Simulator::Destroy();

    if (g_metricsCsv.is_open()) {
        g_metricsCsv.close();
    }

    std::cout << "\n[Success] Full 25-second 5-server simulation completed!" << std::endl;
    return 0;
}
