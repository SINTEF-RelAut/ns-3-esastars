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
    std::vector<Node*> nodes_to_run_next;

//    void RunParallelEvents (host_addr_t host_addr) {
//        omp_set_num_threads(NUM_CORE);
//#pragma omp parallel for schedule (dynamic)
//        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
//            Ptr<SCION_AS> node = dynamic_cast<SCION_AS*>(PeekPointer(nodes.Get(i)));
//            (dynamic_cast<TimeServer*>(node->GetHost(host_addr)))->ConstructSetOfMostDisjointPaths();
//        }
//
//#pragma omp parallel for schedule (dynamic)
//        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
//            Ptr<SCION_AS> node = dynamic_cast<SCION_AS*>(PeekPointer(nodes.Get(i)));
//            (dynamic_cast<TimeServer*>(node->GetHost(host_addr)))->ReadOrWriteDisjointPaths();
//        }
//    }

    template <typename FUNC, typename CLAS>
    void RunParallelEvents (host_addr_t host_addr, std::pair<FUNC, CLAS> mem_obj_pair) {
        FUNC mem_ptr = mem_obj_pair.first;
        omp_set_num_threads(NUM_CORE);
#pragma omp parallel for schedule (dynamic)
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = dynamic_cast<SCION_AS *>(PeekPointer(nodes.Get(i)));
            if (host_addr == 0) {
                ((dynamic_cast<CLAS>(node->GetBeaconServer()))->*mem_ptr)();
            } else if (host_addr == 1) {
                ((dynamic_cast<CLAS>(node->GetPathServer()))->*mem_ptr)();
            } else {
                ((dynamic_cast<CLAS>(node->GetHost(host_addr)))->*mem_ptr)();
            }
        }
    }

    void ExecuteLocallyScheduledEvents (NodeContainer& nodes) {
        auto start = std::chrono::system_clock::now();
#pragma omp parallel for schedule(dynamic, 1)
        for (uint32_t i = 0; i < nodes_to_run_next.size(); ++i) {
            Ptr<SCION_AS> node = dynamic_cast<SCION_AS*>(nodes_to_run_next.at(i));
            node->ExecuteLocalScheduler();
        }
        auto end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end-start;
        NS_LOG_DEBUG("parallel time " << elapsed_seconds.count());

        start = std::chrono::system_clock::now();
        ScheduleNextEvent (nodes);
        end = std::chrono::system_clock::now();
        elapsed_seconds = end-start;
        NS_LOG_DEBUG("serial time " << elapsed_seconds.count());

    }

    void ScheduleNextEvent (NodeContainer& nodes) {
        uint64_t min_event_time = (uint64_t) std::numeric_limits<uint64_t>::max();
        nodes_to_run_next.clear();
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));
            uint64_t event_time = node->GetFirstEventTime();
            if (event_time == min_event_time) {
                nodes_to_run_next.push_back(PeekPointer(node));
            }
            if (event_time < min_event_time) {
                min_event_time = event_time;
                nodes_to_run_next.clear();
                nodes_to_run_next.push_back(PeekPointer(node));
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

    void SchedulePeriodicEvents(YAML::Node& config, NodeContainer& nodes) {
        ia_t printer_ia = DynamicCast<SCION_AS>(nodes.Get(0))->ia_addr;
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<SCION_AS> node = DynamicCast<SCION_AS>(nodes.Get(i));

            if (config["beacon_service"]) {
                node->GetBeaconServer()->ScheduleBeaconing(Time(config["beacon_service"]["last_beaconing"].as<std::string>()));
            }

            if (config["time_service"]) {
                dynamic_cast<TimeServer*>( node->GetHost(2))->ScheduleListOfAllASesRequest();
                dynamic_cast<TimeServer*>( node->GetHost(2))->ScheduleTimeSync(printer_ia);
            }

        }

        ScheduleNextEvent(nodes);

        if (config["beacon_service"]) {
            for (Time t = ns3::Seconds(0.0); t < Time(config["beacon_service"]["last_beaconing"].as<std::string>()); t += Time(config["beacon_service"]["period"].as<std::string>())) {
                Simulator::Schedule(t + ns3::Seconds(1.0), &PeriodicBeaconingCheckPoint, nodes);
            }
        }
    }

    void PeriodicBeaconingCheckPoint(NodeContainer& nodes) {
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
