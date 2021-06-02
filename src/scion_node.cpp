/**
 * @file scion_node.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_node.h
 * @brief Defines the member functions of the SCION_Node.
 */

#include "src/SCION/headers/scion_node.h"
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/beaconing_strategy.h"
#include "ns3/core-module.h"

namespace ns3 {
/**
 * The intra as latencies are estimated by calculating the great circle latencies
 * based on the interface coordinates. Also initializes AS_max_bwd with the highest
 * bandwith found at any border router of this AS.
 *
 * @see calculate_great_circle_latency
 */

void
SCION_Node::DoInitializations() {
        intra_as_latencies.resize (GetNDevices ());
        for (uint64_t i = 0; i < GetNDevices (); ++i)
        {
            intra_as_latencies.at (i).resize (GetNDevices ());
        }

        for (uint32_t i = 0; i < GetNDevices (); ++i)
        {
            for (uint32_t j = i + 1; j < GetNDevices (); ++j)
            {
                intra_as_latencies.at (i).at (j) = calculate_great_circle_latency (
                        interfaces_coordinates.at (i).first, interfaces_coordinates.at (i).second,
                        interfaces_coordinates.at (j).first, interfaces_coordinates.at (j).second);
                intra_as_latencies.at (j).at (i) = intra_as_latencies.at (i).at (j);
            }
        }

        AS_max_bwd = 0;
        for (auto const curr_bwd : inter_as_bwds)
        {
            if (curr_bwd > AS_max_bwd)
            {
                AS_max_bwd = curr_bwd;
            }
        }
}

void
SCION_Node::DoInitializations (uint32_t all_nodes)
{

    DoInitializations();
    strategy->DoInitializations(all_nodes);

}

/**
 * Prints the number of ASes that are reachable until now, updates the now field of the node to current simulator time and initializes the structure
 * which will be filled with the number of bytes sent on each interface during the next beaconing period.
 */
void
SCION_Node::UpdateTimeAndStats ()
{
    // Print statistics until now to see some sense of progress
    //    std::cout << as_number << "\t" << valid_beacons_count_per_dst_as.size ()
    //              << std::endl; // Print number of source ASes
    // Update node-> now for the regular beaconing execution flow
    now = (uint16_t) Simulator::Now ().ToInteger (Time::MIN);
    next_period = now + (uint16_t) beaconing_period.ToInteger (Time::MIN);

    // TODO: Since the # of neighbours is fixed, we could use an Array here instead of a vector for a bit less overhead & for cache optimisation (?).
    bytes_sent_per_interface_per_period.insert (
        std::make_pair (now, std::vector<uint32_t> (GetNDevices (), 0)));
}

/**
 * @param node The node on which the egress interface is connected.
 * @param egress_interface_no The number of the egress interface.
 * @return A pair holding the remote ingress interface number and the remote AS number.
 */
    std::pair<uint16_t, Ptr<SCION_Node>>
    SCION_Node::GetRemoteAsInfo (uint16_t egress_interface_no)
    {
        Ptr<PointToPointNetDevice> self_egress_device = DynamicCast<PointToPointNetDevice> (GetDevice (egress_interface_no));

        Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel> (self_egress_device->GetChannel ());
        uint32_t wire = self_egress_device == channel->GetSource (0) ? 0 : 1;
        Ptr<PointToPointNetDevice> remote_device = channel->GetDestination (wire);

        uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex ();

        Ptr<SCION_Node> remote_as = (DynamicCast<SCION_Node> (remote_device->GetNode ()));

        return std::make_pair (remote_ingress_if_no, remote_as);
    }

/**
 * Calculates the link-level and as-level path diversity scores and a quality metric
 * (based on the latency and bandwidth coefficients in the beacons and the node) for each beacon.
 * Saves the numeric value and the distribution (frequency of occurrence of a certain score) in the passed maps.
 *
 * @see calculate_final_diversity_scores
 *
 * @param satisfaction_stat Is filled with the seen satisfaction scores for each beacon.
 * @param AS_level_diversity_stat Is filled with the AS level diversity scores seen for each beacon.
 * @param link_level_diversity_stat Is filled with the link level diversity scores seen for each beacon.
 */
void
SCION_Node::FinalPathEvaluation (std::map<ld, uint64_t> &satisfaction_stat,
                                 std::map<ld, uint64_t> &AS_level_diversity_stat,
                                 std::map<ld, uint64_t> &link_level_diversity_stat)
{
    for (auto const &the_beacon_pair : path_map_to_beacon)
    {
        beacon *the_beacon = the_beacon_pair.second;
        if (the_beacon->is_valid)
        {
            std::pair<ld, ld> diversity_scores =
                calculate_final_diversity_scores (the_beacon);
            ld AS_level_diversity_score = diversity_scores.first;
            ld link_level_diversity_score = diversity_scores.second;
            ld latency_score = 1 - the_beacon->latency_stat / 1000;
            ld bwd_score = the_beacon->bwd_stat / AS_max_bwd;

            ld score = (latency_score * latency_coef + bwd_score * bandwidth_coef +
                        AS_level_diversity_score * AS_level_diversity_coef +
                        link_level_diversity_score * link_level_diversity_coef) /
                       (latency_coef + bandwidth_coef + AS_level_diversity_coef +
                        link_level_diversity_coef);

            score = roundl (score * 1000) / 1000;
            AS_level_diversity_score = roundl (AS_level_diversity_score * 1000) / 1000;
            link_level_diversity_score = roundl (link_level_diversity_score * 1000) / 1000;

            if (satisfaction_stat.find (score) != satisfaction_stat.end ())
            {
                satisfaction_stat.at (score)++;
            }
            else
            {
                satisfaction_stat.insert (std::make_pair (score, 1));
            }

            if (AS_level_diversity_stat.find (AS_level_diversity_score) !=
                AS_level_diversity_stat.end ())
            {
                AS_level_diversity_stat.at (AS_level_diversity_score)++;
            }
            else
            {
                AS_level_diversity_stat.insert (
                    std::make_pair (AS_level_diversity_score, 1));
            }

            if (link_level_diversity_stat.find (link_level_diversity_score) !=
                link_level_diversity_stat.end ())
            {
                link_level_diversity_stat.at (link_level_diversity_score)++;
            }
            else
            {
                link_level_diversity_stat.insert (
                    std::make_pair (link_level_diversity_score, 1));
            }
        }
    }
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
SCION_Node::calculate_final_diversity_scores (beacon *the_beacon)
{
    ld AS_level_diversity_score = 0;
    ld link_level_diversity_score = 0;
    int32_t counter = 0;

    uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at (0));
    const beacons_with_same_dst_as &equal_dst_as_beacons = beacon_store.at (dst_as);
    for (auto const &len_beacons_pair : equal_dst_as_beacons)
    {
        auto const & beacons = len_beacons_pair.second;
        for (auto const &curr_beacon : beacons)
        {
            if (curr_beacon != the_beacon)
            {
                AS_level_diversity_score +=
                    AS_level_jaccard_distance_between_two_paths (the_beacon,
                                                                 curr_beacon);
                link_level_diversity_score +=
                    link_level_jaccard_distance_between_two_paths (the_beacon,
                                                                   curr_beacon);
                counter++;
            }
        }
    }

    return (
        std::make_pair (AS_level_diversity_score / counter, link_level_diversity_score / counter));
}


void
SCION_Node::IncrementControlPlaneBytesSent(beacon &the_beacon, uint16_t interface) {
    bytes_sent_per_interface_per_period.at(now).at(interface) +=
            (BEACON_HEADER_SIZE + BEACON_HOP_SIZE * the_beacon.the_path.size());
}

void
SCION_Node::ReceiveBeacon(beacon &received_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if) {

    uint16_t dst_as = UPPER_16_BITS(received_beacon.the_path.at(0));
    bool to_import = strategy->ImportPolicy(received_beacon, sender_as, remote_if, local_if, now);
    bool update_strategy_metadata = false;

    if (!to_import){
        return;
    }

    beacon* beacon_in_the_store = NULL;

    if (next_round_valid_beacons_count_per_dst_as.find(dst_as) == next_round_valid_beacons_count_per_dst_as.end()) {
        next_round_valid_beacons_count_per_dst_as.insert(std::make_pair(dst_as, 0));
    }

    if (path_map_to_beacon.find(received_beacon.key) != path_map_to_beacon.end()) {
        beacon_in_the_store = path_map_to_beacon.at(received_beacon.key);

        beacon_in_the_store->next_initiation_time = received_beacon.next_initiation_time;
        beacon_in_the_store->next_expiration_time = received_beacon.next_expiration_time;

        if (!beacon_in_the_store->is_valid) {
            next_round_valid_beacons_count_per_dst_as.at(dst_as)++;
            update_strategy_metadata = true;
        }
    } else {
        next_round_valid_beacons_count_per_dst_as.at(dst_as)++;
        update_strategy_metadata = true;

        beacon_in_the_store = new beacon(received_beacon);
        path_map_to_beacon.insert(std::make_pair(beacon_in_the_store->key, beacon_in_the_store));

        uint16_t path_len = (uint16_t) beacon_in_the_store->the_path.size();

        if (beacon_store.find(dst_as) != beacon_store.end() &&
            beacon_store.at(dst_as).find(path_len) !=
            beacon_store.at(dst_as).end()) {
            beacon_store.at(dst_as).at(path_len).insert(beacon_in_the_store);
        } else if (beacon_store.find(dst_as) != beacon_store.end() &&
                   beacon_store.at(dst_as).find(path_len) ==
                   beacon_store.at(dst_as).end()) {
            beacon_store.at(dst_as).insert(std::make_pair(path_len, beacons_with_equal_length()));
            beacon_store.at(dst_as).at(path_len).insert(beacon_in_the_store);
        } else {
            beacon_store.insert(std::make_pair(dst_as, beacons_with_same_dst_as()));
            beacon_store.at(dst_as).insert(
                    std::make_pair(path_len, beacons_with_equal_length()));
            beacon_store.at(dst_as).at(path_len).insert(beacon_in_the_store);
        }
    }

    if (update_strategy_metadata) {
        strategy->UpdateStrategyMetaDataAfterImport(beacon_in_the_store, sender_as, remote_if, local_if);
    }
}

} // namespace ns3