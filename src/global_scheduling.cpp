//
// Created by seyedali on 17.07.21.
//

#include <omp.h>
#include <chrono>

#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"

#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/global_scheduling.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/time_server.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("GlobalScheduling");

    void SchedulePeriodicEvents(YAML::Node& config) {
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));

            if (config["beacon_service"]) {
                node->GetBeaconServer()->ScheduleBeaconing(Time(config["beacon_service"]["last_beaconing"].as<std::string>()));
            }

            if (config["time_service"]) {
                dynamic_cast<TimeServer*>( node->GetHost(2))->ScheduleListOfAllASesRequest();
                dynamic_cast<TimeServer*>( node->GetHost(2))->ScheduleTimeSync();
                dynamic_cast<TimeServer*>( node->GetHost(2))->ScheduleSnapShots();
            }

        }

        if (config["beacon_service"]) {
            for (Time t = ns3::Seconds(0.0); t < Time(config["beacon_service"]["last_beaconing"].as<std::string>()); t += Time(config["beacon_service"]["period"].as<std::string>())) {
                Simulator::Schedule(t + ns3::Seconds(1.0), &PeriodicBeaconingCheckPoint);
            }
        }

    }

    void PeriodicBeaconingCheckPoint() {
        std::cout << "################################## " << DynamicCast<SCION_AS>(nodes.Get(0))->GetBeaconServer()->GetCurrentTime() << " #########################################" << std::endl;
        uint32_t node_number = nodes.GetN();

        // print number of connected pairs after each beaconing round
        uint32_t all_connected_pairs = 0;
        for (uint32_t i = 0; i < node_number; ++i) {
            all_connected_pairs += DynamicCast<SCION_AS>(nodes.Get(i))->GetBeaconServer()->valid_beacons_count_per_dst_as.size();
        }
        std::cout << all_connected_pairs << std::endl;

    }
}
