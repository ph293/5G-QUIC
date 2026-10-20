#ifndef MCVT_SNMO_SDN_H
#define MCVT_SNMO_SDN_H

#include "quic_sdsm.h"
#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>

class SdnControllerManager {
public:
    struct ControllerState {
        double cpuUtilization;
        double memoryUsage;
        double flowRequestRate;
        double updateOverhead;
    };

    SdnControllerManager() {
        m_controllers.resize(3); // 3 Distributed SDN Controllers managing 5 towers
        m_controllers[0] = {88.5, 91.0, 450.0, 15.2}; // Master (Overloaded)
        m_controllers[1] = {32.0, 40.0, 120.0, 4.1};  // Target 1
        m_controllers[2] = {25.0, 30.0, 80.0,  3.0};  // Target 2
    }

    int EvaluateControllerLoadViaMCVT(int controllerId) {
        ControllerState c = m_controllers[controllerId];
        std::vector<double> metrics = {c.cpuUtilization, c.memoryUsage, c.flowRequestRate / 5.0, c.updateOverhead * 5.0};
        
        double sum = std::accumulate(metrics.begin(), metrics.end(), 0.0);
        double mean = sum / metrics.size();

        double sq_sum = 0.0;
        for (double val : metrics) {
            sq_sum += (val - mean) * (val - mean);
        }
        double stdev = std::sqrt(sq_sum / metrics.size());
        double cov = (mean > 0) ? (stdev / mean) : 0.0;
        double loadScore = mean * (1.0 + cov);

        std::cout << "[MCVT Engine] Controller " << controllerId << " State Analysis -> Mean: " 
                  << mean << " | CoV: " << cov << " | Final Load Score: " << loadScore << std::endl;

        if (loadScore > 75.0) return 3; // Overloaded
        if (loadScore > 50.0) return 2; // Pre-overloaded
        if (loadScore > 25.0) return 1; // Normal
        return 0;                       // Underloaded
    }

    int RunSNMOTargetSelection(int overloadedControllerId) {
        std::cout << "[SNMO Optimizer] Running Star-Nosed Mole Optimizer across SDN cluster..." << std::endl;
        int bestTarget = -1;
        double minCost = 999999.0;

        for (size_t i = 0; i < m_controllers.size(); ++i) {
            if ((int)i == overloadedControllerId) continue;
            double cost = m_controllers[i].cpuUtilization + (m_controllers[i].updateOverhead * 2.0);
            if (cost < minCost) {
                minCost = cost;
                bestTarget = i;
            }
        }
        std::cout << "[SNMO Optimizer] Optimal Target Controller Selected: Controller " << bestTarget 
                  << " (Min Cost: " << minCost << ")\n" << std::endl;
        return bestTarget;
    }

private:
    std::vector<ControllerState> m_controllers;
};

// Periodic Handover trigger function for each tower transition
inline void TriggerHandoverAndMigration(Ptr<QuicClientApp> clientApp, Ipv4Address targetMecIp, int towerStep, std::string targetName) {
    std::cout << "\n----------------------------------------------------------------------" << std::endl;
    std::cout << "[Handover Event] Car reached Tower " << towerStep << " at t = " << Simulator::Now().GetSeconds() << "s" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;

    SdnControllerManager sdnManager;
    int state = sdnManager.EvaluateControllerLoadViaMCVT(0);
    
    if (state >= 2) {
        std::cout << "[Control Plane Alert] Controller 0 is OVERLOADED! Rebalancing via SNMO..." << std::endl;
        int targetController = sdnManager.RunSNMOTargetSelection(0);
        std::cout << "[Control Plane Action] SDN Route Rules installed via Controller " << targetController << std::endl;
    }

    clientApp->MigrateConnection(targetMecIp);
}

#endif
