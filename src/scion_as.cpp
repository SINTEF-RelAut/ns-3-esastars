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

    void SCION_AS::DoInitializations(uint32_t num_ASes) {
        initialize_latencies(true);

        AS_max_bwd = 0;
        for (auto const curr_bwd : inter_as_bwds) {
            if (curr_bwd > AS_max_bwd) {
                AS_max_bwd = curr_bwd;
            }
        }

        beaconServer->DoInitializations(num_ASes);
    }

    void
    SCION_AS::DoInitializations(uint32_t num_ASes, bool only_propagation_delay, std::string border_routers_malicious_action, Time malicious_delay) {
        connect_internal_nodes(only_propagation_delay,  border_routers_malicious_action, malicious_delay);
        initialize_latencies(only_propagation_delay);

        for (auto const & br : border_routers) {
            br->InitializeTransmissionQueues();
        }

        for (auto const & host : hosts) {
            host->InitializeTransmissionQueues();
        }

        pathServer->InitializeTransmissionQueues();

        AS_max_bwd = 0;
        for (auto const curr_bwd : inter_as_bwds) {
            if (curr_bwd > AS_max_bwd) {
                AS_max_bwd = curr_bwd;
            }
        }

        beaconServer->DoInitializations(num_ASes);
    }

    std::pair<uint16_t, SCION_AS*>
    SCION_AS::GetRemoteAsInfo(uint16_t egress_interface_no) {
        return remote_as_info.at(egress_interface_no);
    }

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

    SCIONCapableNode *
    SCION_AS::GetHost(host_addr_t host_addr) {
        if (host_addr == 1) {
            return GetPathServer();
        }

        return ((SCIONCapableNode *) hosts.at(host_addr - 2));
    }

    uint32_t SCION_AS::GetNHosts() {
        return hosts.size();
    }

    void
    SCION_AS::AddHost(SCIONHost* host) {
        hosts.push_back(host);
    }

    BorderRouter* SCION_AS::AddBR(double latitude, double longitude, Time processing_delay, Time processing_throughput_delay) {
        BorderRouter* the_br = new BorderRouter(0,  isd_number,  as_number,  0, latitude,  longitude, this);

        the_br->SetProcessingDelay(processing_delay, processing_throughput_delay);

        border_routers.push_back(the_br);
        interfaces_coordinates.push_back(std::pair<ld, ld>(latitude, longitude));

        return the_br;
    }

    void SCION_AS::connect_internal_nodes(bool only_propagation_delay, std::string border_routers_malicious_action, Time malicious_delay) {
        std::map<BorderRouter*, std::set<uint16_t>> border_router_to_if;

        for (uint16_t i = 0; i < GetNDevices(); ++i) {
            BorderRouter* br = border_routers.at(i);
            if (border_router_to_if.find(br) == border_router_to_if.end()) {
                border_router_to_if.insert(std::make_pair(br, std::set<uint16_t>()));
            }
            border_router_to_if.at(br).insert(i);
        }

        std::set<BorderRouter*> border_routers_set (border_routers.begin(), border_routers.end());
        std::vector<BorderRouter*> border_routers_vec (border_routers_set.begin(), border_routers_set.end());

        // Connect border routers to border routers
        for (uint32_t i = 0; i < border_routers_vec.size() - 1; ++i) {
            BorderRouter* br1 = border_routers_vec.at(i);
            for (uint32_t j = i + 1; j < border_routers_vec.size(); ++j) {
                BorderRouter* br2 = border_routers_vec.at(j);

                Time propagation_delay1 = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) br1->GetLatitude(), (ld) br1->GetLogitude(), (ld) br2->GetLatitude(), (ld) br2->GetLogitude())));
                Time propagation_delay2 = propagation_delay1;

                if (border_routers_malicious_action == "symmetric_delay") {
                    propagation_delay1 += malicious_delay;
                    propagation_delay2 += malicious_delay;
                } else if (border_routers_malicious_action == "asymmetric_delay") {
                    propagation_delay1 += malicious_delay;
                }

                br1->AddToPropagationDelays(propagation_delay1);
                br2->AddToPropagationDelays(propagation_delay2);

                if (only_propagation_delay) {
                    br1->AddToTransmissionDelays(Time(0)); // transmission delay for one byte assuming 400 Gbps link
                    br2->AddToTransmissionDelays(Time(0));
                } else {
                    br1->AddToTransmissionDelays(PicoSeconds(20)); // transmission delay for one byte assuming 400 Gbps link
                    br2->AddToTransmissionDelays(PicoSeconds(20));
                }

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

        // Connect border routers to hosts
        for (uint32_t i = 0; i < border_routers_vec.size(); ++i) {
            BorderRouter* br = border_routers_vec.at(i);
            for (uint32_t j = 0; j < hosts.size(); ++j) {
                SCIONHost* host = hosts.at(j);

                Time propagation_delay = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) br->GetLatitude(), (ld) br->GetLogitude(), (ld) host->GetLatitude(), (ld) host->GetLogitude())));

                br->AddToPropagationDelays(propagation_delay);
                host->AddToPropagationDelays(propagation_delay);

                if (only_propagation_delay) {
                    br->AddToTransmissionDelays(Time(0)); // transmission delay for one byte assuming 1 Gbps link
                    host->AddToTransmissionDelays(Time(0));
                } else {
                    br->AddToTransmissionDelays(NanoSeconds(8)); // transmission delay for one byte assuming 1 Gbps link
                    host->AddToTransmissionDelays(NanoSeconds(8));
                }

                br->AddToRemoteNodesInfo(host, host->GetNDevices() - 1, isd_number, as_number);
                host->AddToRemoteNodesInfo(br, br->GetNDevices() - 1, isd_number, as_number);

                for (uint16_t as_if : border_router_to_if.at(br)) {
                    host->AddToIFForwadingTable(as_if, host->GetNDevices() - 1);
                }

                br->AddToAddressForwardingTable(host->GetLocalAddress(), br->GetNDevices() - 1);
            }
        }

        // Connect border routers to path server
        for (uint32_t i = 0; i < border_routers_vec.size(); ++i) {
            BorderRouter* br = border_routers_vec.at(i);

            Time propagation_delay = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) br->GetLatitude(), (ld) br->GetLogitude(), (ld) pathServer->GetLatitude(), (ld) pathServer->GetLogitude())));

            br->AddToPropagationDelays(propagation_delay);
            pathServer->AddToPropagationDelays(propagation_delay);

            if (only_propagation_delay) {
                br->AddToTransmissionDelays(Time(0)); // transmission delay for one byte assuming 10 Gbps link
                pathServer->AddToTransmissionDelays(Time(0));
            } else {
                br->AddToTransmissionDelays(PicoSeconds(800)); // transmission delay for one byte assuming 10 Gbps link
                pathServer->AddToTransmissionDelays(PicoSeconds(800));
            }

            br->AddToRemoteNodesInfo(pathServer, pathServer->GetNDevices() - 1, isd_number, as_number);
            pathServer->AddToRemoteNodesInfo(br, br->GetNDevices() - 1, isd_number, as_number);

            for (uint16_t as_if : border_router_to_if.at(br)) {
                pathServer->AddToIFForwadingTable(as_if, pathServer->GetNDevices() - 1);
            }

            br->AddToAddressForwardingTable(pathServer->GetLocalAddress(), br->GetNDevices() - 1);
        }

        // Connect hosts to local path server
        for (uint32_t i = 0; i < hosts.size(); ++i) {
            SCIONHost* host = hosts.at(i);

            Time propagation_delay = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) host->GetLatitude(), (ld) host->GetLogitude(), (ld) pathServer->GetLatitude(), (ld) pathServer->GetLogitude())));

            host->AddToPropagationDelays(propagation_delay);
            pathServer->AddToPropagationDelays(propagation_delay);

            if (only_propagation_delay) {
                host->AddToTransmissionDelays(Time(0)); // transmission delay for one byte assuming 1 Gbps link
                pathServer->AddToTransmissionDelays(Time(0));
            } else {
                host->AddToTransmissionDelays(NanoSeconds(8)); // transmission delay for one byte assuming 1 Gbps link
                pathServer->AddToTransmissionDelays(NanoSeconds(8));
            }

            host->AddToRemoteNodesInfo(pathServer, pathServer->GetNDevices() - 1, isd_number, as_number);
            pathServer->AddToRemoteNodesInfo(host, host->GetNDevices() - 1, isd_number, as_number);

            host->AddToAddressForwardingTable(pathServer->GetLocalAddress(), host->GetNDevices() - 1);
            pathServer->AddToAddressForwardingTable(host->GetLocalAddress(), pathServer->GetNDevices() - 1);
        }

        // Connect hosts to each other
        for (uint32_t i = 0; i < hosts.size() - 1; ++i) {
            SCIONHost* h1 = hosts.at(i);
            for (uint32_t j = i + 1; j < hosts.size(); ++j) {
                SCIONHost* h2 = hosts.at(j);

                Time propagation_delay = NanoSeconds((int64_t) floor(1e6 * calculate_great_circle_latency((ld) h1->GetLatitude(), (ld) h1->GetLogitude(), (ld) h2->GetLatitude(), (ld) h2->GetLogitude())));

                h1->AddToPropagationDelays(propagation_delay);
                h2->AddToPropagationDelays(propagation_delay);

                if (only_propagation_delay) {
                    h1->AddToTransmissionDelays(Time(0)); // transmission delay for one byte assuming 1 Gbps link
                    h2->AddToTransmissionDelays(Time(0));
                } else {
                    h1->AddToTransmissionDelays(NanoSeconds(8)); // transmission delay for one byte assuming 1 Gbps link
                    h2->AddToTransmissionDelays(NanoSeconds(8));
                }

                h1->AddToRemoteNodesInfo(h2, h2->GetNDevices() - 1, isd_number, as_number);
                h2->AddToRemoteNodesInfo(h1, h1->GetNDevices() - 1, isd_number, as_number);

                h1->AddToAddressForwardingTable(h2->GetLocalAddress(), h1->GetNDevices() - 1);
                h2->AddToAddressForwardingTable(h1->GetLocalAddress(), h2->GetNDevices() - 1);
            }
        }
    }

    void SCION_AS::initialize_latencies(bool only_propagation_delay) {
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

    void SCION_AS::AddToRemoteASInfo (uint16_t remote_if, SCION_AS* remote_as) {
        remote_as_info.push_back(std::make_pair(remote_if, remote_as));
    }
}