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
        latencies_between_interfaces.resize(GetNDevices());
        events.resize(GetNDevices() + 2 + hosts.size()); // 3 = path server + beacon server +  hosts

        for (uint64_t i = 0; i < GetNDevices(); ++i) {
            latencies_between_interfaces.at(i).resize(GetNDevices());
        }

        for (uint64_t i = 0; i < GetNDevices() + 2 + hosts.size(); ++i) {
            events.at(i) = new LocalScheduler();
        }


        for (uint32_t i = 0; i < GetNDevices(); ++i) {
            for (uint32_t j = i + 1; j < GetNDevices(); ++j) {
                latencies_between_interfaces.at(i).at(j) = calculate_great_circle_latency(
                        interfaces_coordinates.at(i).first, interfaces_coordinates.at(i).second,
                        interfaces_coordinates.at(j).first, interfaces_coordinates.at(j).second);
                latencies_between_interfaces.at(j).at(i) = latencies_between_interfaces.at(i).at(j);
            }
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
        return GetNDevices() + 1;
    }

    uint32_t
    SCION_AS::GetBeaconServerSchedulerIdx() {
        return GetNDevices();
    }

    uint32_t
    SCION_AS::GetHostSchedulerIdx(uint32_t host_addr) {
        return GetNDevices() + 2 + host_addr;
    }

    SCIONHost*
    SCION_AS::GetHost(uint32_t host_addr) {
        return hosts.at(host_addr);
    }

    void
    SCION_AS::AddHost(SCIONHost* host) {
        hosts.push_back(host);
    }
}