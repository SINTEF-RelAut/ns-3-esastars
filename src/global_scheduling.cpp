//
// Created by seyedali on 17.07.21.
//
#include <ns3/simulator.h>
#include "ns3/ptr.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include <ns3/nstime.h>
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/global_scheduling.h"
#include <omp.h>

namespace ns3 {
    void ExecuteNonPeriodicEvents (NodeContainer nodes, Time advance){
#pragma omp parallel for
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            node->AdvanceTime(advance);
            node->ExecuteNonPeriodicEvents();
        }
    }

    void SchedulePeriodicEvents(NodeContainer& nodes, Time beaconing_period, Time last_beaconing_event_time, Time simulation_end) {
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            node->ScheduleBeaconing(beaconing_period, last_beaconing_event_time);
        }

        for (Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
            ns3::Simulator::Schedule(t + ns3::Seconds(1.0), &PeriodicCheckPoint, nodes);
        }

        Time advance_step = ns3::MicroSeconds(100);
        for (Time t = ns3::Seconds(0.0); t < simulation_end; t += advance_step) {
            ns3::Simulator::Schedule(t, &ExecuteNonPeriodicEvents, nodes, advance_step);
        }

    }

    void PeriodicCheckPoint(NodeContainer nodes) {
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
