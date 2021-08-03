//
// Created by seyedali on 17.07.21.
//

#include <omp.h>

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

namespace ns3 {
    void ExecuteLocallyScheduledEvents (NodeContainer nodes) {
#pragma omp parallel for
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            node->ExecuteLocalScheduler();
        }

        ScheduleNextEvent (nodes);
    }

    void ScheduleNextEvent (NodeContainer nodes) {
        uint64_t min_event_time = (uint64_t) std::numeric_limits<uint64_t>::max();
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            uint64_t event_time = node->GetFirstEventTime();
            if (event_time < min_event_time) {
                min_event_time = event_time;
            }
        }

        if (min_event_time == std::numeric_limits<uint64_t>::max()) {
            return;
        }

        Time advance = TimeStep(min_event_time) -  Simulator::Now();
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            node->AdvanceTime(advance);
        }

        Simulator::Schedule(advance, &ExecuteLocallyScheduledEvents, nodes);
    }

    void SchedulePeriodicEvents(NodeContainer& nodes, Time beaconing_period, Time last_beaconing_event_time, Time simulation_end) {


        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            node->GetBeaconServer()->ScheduleBeaconing(last_beaconing_event_time);

            if (i == 0) {
                node->GetHost(2)->GetProcessScheduler()->Schedule(Minutes(180),
                                   &SCIONHost::SendArbitraryPacket,
                                   node->GetHost(2),
                                   DynamicCast<SCION_AS>(nodes.Get(1))->ia_addr, 2);
            }
        }

        ScheduleNextEvent(nodes);

        for (Time t = ns3::Seconds(0.0); t < last_beaconing_event_time; t += beaconing_period) {
            Simulator::Schedule(t + ns3::Seconds(1.0), &PeriodicCheckPoint, nodes);
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
