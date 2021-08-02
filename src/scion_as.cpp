/**
 * @file scion_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 *
 * @brief Implements the specialized functions on the scion leaf ASes.
 */

#include "ns3/core-module.h"

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {

    void
    SCION_AS::DoInitializations() {
        connect_internal_nodes();
        initialize_latencies();
        initialize_schedulers();

        for (auto const & br : border_routers) {
            br->InitializeTransmissionQueues();
        }

        for (auto const & host : hosts) {
            host->InitializeTransmissionQueues();
        }

        AS_max_bwd = 0;
        for (auto const curr_bwd : inter_as_bwds) {
            if (curr_bwd > AS_max_bwd) {
                AS_max_bwd = curr_bwd;
            }
        }
    }

    void
    SCION_AS::DoInitializations(uint32_t all_nodes) {

        DoInitializations();
        beaconServer->DoInitializations(all_nodes);

    }

/**
 * @param node The node on which the egress interface is connected.
 * @param egress_interface_no The number of the egress interface.
 * @return A pair holding the remote ingress interface number and the remote AS number.
 */
    std::pair<uint16_t, Ptr<SCION_AS>>
    SCION_AS::GetRemoteAsInfo(uint16_t egress_interface_no) {
        Ptr<PointToPointNetDevice> self_egress_device = DynamicCast<PointToPointNetDevice>(
                GetDevice(egress_interface_no));

        Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(self_egress_device->GetChannel());
        uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
        Ptr<PointToPointNetDevice> remote_device = channel->GetDestination(wire);

        uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();

        Ptr<SCION_AS> remote_as = (DynamicCast<SCION_AS>(remote_device->GetNode()));

        return std::make_pair(remote_ingress_if_no, remote_as);
    }


/**
 *  Iterates over all the beacons which originated at the same source AS then the passed beacon and computes
 *  the average AS level diversity and link level diversity scores.
 *  @see AS_level_jaccard_distance_between_two_paths
 *  @see link_level_jaccard_distance_between_two_paths
 *
 * @param the_beacon The beacon holding the path for which you would like to get the diversity scores.
 * @return Pair(Average AS-lvl diversity, Average link-lvl diversity) of the passed beacon.
 */
    std::pair<ld, ld>
    SCION_AS::calculate_final_diversity_scores(Beacon *the_beacon) {
        ld AS_level_diversity_score = 0;
        ld link_level_diversity_score = 0;
        int32_t counter = 0;

        uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at(0));
        auto const &equal_dst_as_beacons = beaconServer->beacon_store.at(dst_as);
        for (auto const &len_beacons_pair : equal_dst_as_beacons) {
            auto const &beacons = len_beacons_pair.second;
            for (auto const &curr_beacon : beacons) {
                if (curr_beacon != the_beacon) {
                    AS_level_diversity_score +=
                            AS_level_jaccard_distance_between_two_paths(the_beacon,
                                                                        curr_beacon);
                    link_level_diversity_score +=
                            link_level_jaccard_distance_between_two_paths(the_beacon,
                                                                          curr_beacon);
                    counter++;
                }
            }
        }

        return (std::make_pair(AS_level_diversity_score / counter, link_level_diversity_score / counter));
    }

    void
    SCION_AS::ReceiveBeacon(Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if) {
        beaconServer->ReceiveBeacon(received_beacon, sender_as, remote_if, local_if);
    }


    void
    SCION_AS::SetBeaconServer(BeaconServer *the_beaconServer) {
        this->beaconServer = the_beaconServer;
    }

    void SCION_AS::AdvanceTime (ns3::Time advance) {
        local_time += advance;
    }

    void SCION_AS::ExecuteLocalScheduler() {
        for (auto const & scheduler : events) {
            scheduler->ProcessEvents();
        }
    }

    uint64_t SCION_AS::GetFirstEventTime () {
        uint64_t min_time = std::numeric_limits<uint64_t>::max();

        for (auto const & scheduler : events) {
            uint64_t event_time = scheduler->GetFirstEventTime();
            if (event_time < min_time) {
                min_time = event_time;
            }
        }

        return min_time;
    }

    BeaconServer *
    SCION_AS::GetBeaconServer() {
        return this->beaconServer;
    }

    PathServer*
    SCION_AS::GetPathServer() {
        return this->pathServer;
    }

    void
    SCION_AS::SetPathServer(PathServer* the_pathServer) {
        this->pathServer = the_pathServer;
    }

    uint32_t
    SCION_AS::GetPathServerSchedulerIdx() {
        return GetNDevices() * 4 + 1;
    }

    uint32_t
    SCION_AS::GetBeaconServerSchedulerIdx() {
        return GetNDevices() * 4;
    }

    uint32_t
    SCION_AS::GetHostSchedulerIdx(host_addr_t host_addr) {
        return GetNDevices() * 4 + 2 + (host_addr - 2) * 5;
    }

    Ptr<SCIONHost>
    SCION_AS::GetHost(host_addr_t host_addr) {
        return hosts.at(host_addr - 2);
    }

    void
    SCION_AS::AddHost(Ptr<SCIONHost> host) {
        hosts.push_back(host);
    }

    Ptr<BorderRouter> SCION_AS::AddBR(double latitude, double longitude, Time processing_delay, Time processing_throughput_delay) {
        Ptr<BorderRouter> the_br = CreateObject<BorderRouter>(0,  isd_number,  as_number,  0,
                                                               latitude,  longitude, GetNDevices() - 1);

        the_br->SetProcessingDelay(processing_delay, processing_throughput_delay);

        border_routers.push_back(the_br);
        interfaces_coordinates.push_back(std::pair<ld, ld>(latitude, longitude));

        return the_br;
    }

    void SCION_AS::connect_internal_nodes() {
        std::map<Ptr<BorderRouter>, std::set<uint16_t>> border_router_to_if;

        for (uint16_t i = 0; i < GetNDevices(); ++i) {
            Ptr<BorderRouter> br = border_routers.at(i);
            if (border_router_to_if.find(br) == border_router_to_if.end()) {
                border_router_to_if.insert(std::make_pair(br, std::set<uint16_t>()));
            }
            border_router_to_if.at(br).insert(i);
        }

        std::set<Ptr<BorderRouter>> border_routers_set (border_routers.begin(), border_routers.end());
        std::vector<Ptr<BorderRouter>> border_routers_vec (border_routers_set.begin(), border_routers_set.end());

        PointToPointHelper helper;

        for (uint32_t i = 0; i < border_routers_vec.size() - 1; ++i) {
            Ptr<BorderRouter> br1 = border_routers_vec.at(i);
            for (uint32_t j = i + 1; j < border_routers_vec.size(); ++j) {
                Ptr<BorderRouter> br2 = border_routers_vec.at(j);

                helper.Install(br1, br2);

                Time propagation_delay = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) br1->GetLatitude(), (ld) br1->GetLogitude(), (ld) br2->GetLogitude(), (ld) br2->GetLogitude())));

                br1->AddToPropagationDelays(propagation_delay);
                br2->AddToPropagationDelays(propagation_delay);

                br1->AddToTransmissionDelays(PicoSeconds(20));
                br2->AddToTransmissionDelays(PicoSeconds(20));// 400 Gbps link

                br1->AddToRemoteNodesInfo(br2, br2->GetNDevices() - 1, isd_number, as_number);
                br2->AddToRemoteNodesInfo(br1, br1->GetNDevices() - 1, isd_number, as_number);

                for (uint16_t as_if : border_router_to_if.at(br1)) {
                    br2->AddToIFForwadingTable(as_if, br2->GetNDevices() - 1);
                }

                for (uint16_t as_if : border_router_to_if.at(br2)) {
                    br1->AddToIFForwadingTable(as_if, br1->GetNDevices() - 1);
                }
            }
        }

        for (uint32_t i = 0; i < border_routers_vec.size(); ++i) {
            Ptr<BorderRouter> br = border_routers_vec.at(i);
            for (uint32_t j = 0; j < hosts.size(); ++j) {
                Ptr<SCIONHost> host = hosts.at(j);
                helper.Install(br, host);

                Time propagation_delay = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) br->GetLatitude(), (ld) br->GetLogitude(), (ld) host->GetLogitude(), (ld) host->GetLogitude())));

                br->AddToPropagationDelays(propagation_delay);
                host->AddToPropagationDelays(propagation_delay);

                br->AddToTransmissionDelays(PicoSeconds(20)); // 400 Gbps link
                host->AddToTransmissionDelays(PicoSeconds(20));

                br->AddToRemoteNodesInfo(host, host->GetNDevices() - 1, isd_number, as_number);
                host->AddToRemoteNodesInfo(br, br->GetNDevices() - 1, isd_number, as_number);

                for (uint16_t as_if : border_router_to_if.at(br)) {
                    host->AddToIFForwadingTable(as_if, host->GetNDevices() - 1);
                }

                br->AddToAddressForwardingTable(host->GetLocalAddress(), br->GetNDevices() - 1);
            }
        }



    }

    void SCION_AS::initialize_schedulers() {
        std::set<Ptr<BorderRouter>> border_routers_set (border_routers.begin(), border_routers.end());
        events.resize(border_routers_set.size() * 4 + 2 + hosts.size() * 5);
        int i = 0;
        for (auto const & br : border_routers_set) {
            auto const & [receive_scheduler_local_as, receive_scheduler_remote_as] = br->GetReceiveSchedulers();
            events.at(i) = receive_scheduler_local_as;
            events.at(i + 1) = receive_scheduler_remote_as;
            events.at(i + 2) = br->GetProcessScheduler();
            events.at(i + 3) = br->GetSendScheduler();
            i += 4;
        }

        events.at(i) = new LocalScheduler();
        events.at(i + 1) = new LocalScheduler();
        i += 2;

        int k = 0;
        for (uint64_t j = i; j < i + hosts.size() * 5; j += 5) {
            Ptr<SCIONHost> host = hosts.at(k);
            auto const & [receive_scheduler_local_as, receive_scheduler_remote_as] = host->GetReceiveSchedulers();
            events.at(j) = new LocalScheduler();
            events.at(j + 1) = receive_scheduler_local_as;
            events.at(j + 2) = receive_scheduler_remote_as;
            events.at(j + 3) = hosts.at(k)->GetProcessScheduler();
            events.at(j + 4) = hosts.at(k)->GetSendScheduler();
            k++;
        }
    }

    void SCION_AS::initialize_latencies() {
        latencies_between_interfaces.resize(GetNDevices());

        for (uint64_t i = 0; i < GetNDevices(); ++i) {
            latencies_between_interfaces.at(i).resize(GetNDevices());
        }

        for (uint32_t i = 0; i < GetNDevices(); ++i) {
            for (uint32_t j = i + 1; j < GetNDevices(); ++j) {
                latencies_between_interfaces.at(i).at(j) = calculate_great_circle_latency(
                        interfaces_coordinates.at(i).first, interfaces_coordinates.at(i).second,
                        interfaces_coordinates.at(j).first, interfaces_coordinates.at(j).second);
                latencies_between_interfaces.at(j).at(i) = latencies_between_interfaces.at(i).at(j);
            }
        }

        latency_between_path_server_and_beacon_server = MilliSeconds(100);
        // host_address == 0 ==> beacon server, host_address == 1 ==> path_server
        latencies_between_hosts_and_path_server.push_back(MilliSeconds(100));
        latencies_between_hosts_and_path_server.push_back(Time(0));

        for (uint32_t i = 0; i < hosts.size(); ++i) {
            latencies_between_hosts_and_path_server.push_back(MilliSeconds(20));
        }
    }
}