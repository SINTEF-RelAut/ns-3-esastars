/**
 * @file latency_optimized_beaconing.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see latency_optimized_beaconing.h
 * @brief Implements the specialized functions for the latency optimized beaconing strategy.
 */

#include <cassert>
#include <omp.h>
#include "../headers/latency_optimized_beaconing.h"
#include "../headers/utils.h"

namespace ns3 {
    void LatencyOptimized::DoInitializations(uint32_t all_nodes) {
        beacons_per_dst_sorted_by_latency.resize(all_nodes);

        for (uint32_t i = 0;i < all_nodes; ++i ) {
            beacons_per_dst_sorted_by_latency.at(i) = std::map<ld, std::set<beacon*>>();
        }
    }

    std::tuple<bool, bool, bool, beacon*>
    LatencyOptimized::ImportPolicy(beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                 uint16_t self_ingress_if_no, uint16_t now)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon.the_path.at(0));

        if (node->path_map_to_beacon.find(the_beacon.key) != node->path_map_to_beacon.end()) {
            beacon* existing_beacon = node->path_map_to_beacon.at(the_beacon.key);
            if (!existing_beacon->is_valid){
                return std::tuple<bool, bool, bool, beacon*>(true, true, false, existing_beacon);
            }
            return std::tuple<bool, bool, bool, beacon*>(true, true, true, existing_beacon);
        }

        if (the_beacon.the_path.size() == 1) {
            return std::tuple<bool, bool, bool, beacon*>(true, false, false, NULL);
        }

        if (node->next_round_valid_beacons_count_per_dst_as.at(dst_as) < MAX_BEACONS_TO_STORE) {
            return std::tuple<bool, bool, bool, beacon*>(true, false, false, NULL);
        }

        assert(beacons_per_dst_sorted_by_latency.size() > dst_as);
        assert(!beacons_per_dst_sorted_by_latency.at(dst_as).empty());
        ld latency = the_beacon.latency_stat;

        std::map<ld, std::set<beacon*>>::reverse_iterator highest_previous_latency_iterator = beacons_per_dst_sorted_by_latency.at(dst_as).rbegin();
        ld highest_previous_latency = highest_previous_latency_iterator->first;
        if (highest_previous_latency > latency) {
            assert(!highest_previous_latency_iterator->second.empty());
            beacon* to_be_removed_beacon = *highest_previous_latency_iterator->second.begin();
            return std::tuple<bool, bool, bool, beacon*> (true, false, false, to_be_removed_beacon);
        }
        return std::tuple<bool, bool, bool, beacon*> (false, false, false, NULL);
    }

    void
    LatencyOptimized::DeleteFromStrategyMetaData (beacon* the_beacon)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        delete_from_beacons_per_dst_sorted_by_latency(dst_as, the_beacon);
    }

    void
    LatencyOptimized::InsertToStrategyMetaData (beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        insert_to_beacons_per_dst_sorted_by_latency(dst_as,  the_beacon);
    }

    void
    LatencyOptimized::DisseminateBeacons(SCION_Node::neighbour_relation relation) {
        uint32_t neighbors_cnt = node->neighbors.size();
        omp_set_num_threads(NUM_CORE);
#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) { // Per neighbor AS

            if (node->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t remote_as_no = node->neighbors.at(i).first;
            for (auto const &dst_as_beacons_pair : node->beacon_store) { // Per destination AS
                uint16_t dst_as_no = dst_as_beacons_pair.first;
                const beacons_with_same_dst_as &beacons_to_the_dst_as = dst_as_beacons_pair.second;

                if (remote_as_no == dst_as_no) {
                    continue;
                }

                std::multimap<ld, std::tuple<beacon *, uint16_t, uint16_t, Ptr<SCION_Node>, ld, ld> > selected_beacons =
                        select_beacons_to_disseminate_per_dst_per_nbr(remote_as_no, dst_as_no, beacons_to_the_dst_as);

                for (auto const &the_tuple_pair : selected_beacons) {
                    beacon *the_beacon;
                    uint16_t remote_ingress_if_no;
                    uint16_t self_egress_if_no;
                    Ptr<SCION_Node> remote_as;
                    ld latency;
                    ld bwd;

                    std::tie(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as, latency,
                             bwd) = the_tuple_pair.second;

                    GenerateBeaconAndSend(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                          latency, bwd, 0, false, 0);

                }
            }
        }
    }

    std::multimap<ld, std::tuple<beacon *, uint16_t, uint16_t, Ptr<SCION_Node>, ld, ld> >
    LatencyOptimized::select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                                  const beacons_with_same_dst_as &beacons_to_the_dst_as) {
        std::multimap<ld, std::tuple<beacon *, uint16_t, uint16_t, Ptr<SCION_Node>, ld, ld> > latency_map_to_beacon_and_metadata;
        std::multimap<ld, std::pair<beacon *, uint16_t> > valid_candidates;

        for (auto const &len_beacons_pair : beacons_to_the_dst_as) {
            auto const &beacons = len_beacons_pair.second;
            for (auto const &the_beacon : beacons) {
                if (!the_beacon->is_valid) {
                    continue;
                }

                bool generates_loop = false;
                for (auto const &link_info : the_beacon->the_path) { // remove loops
                    if (UPPER_16_BITS(link_info) == remote_as_no) {
                        generates_loop = true;
                        break;
                    }
                }

                if (generates_loop) {
                    continue;
                }

                auto const &interfaces = node->interfaces_per_neighbor_as.at(remote_as_no);
                for (auto const &self_egress_if_no : interfaces) {
                    ld  latency = the_beacon->latency_stat +
                                  node->intra_as_latencies.at(LOWER_16_BITS(the_beacon->the_path.back())).at(self_egress_if_no);

                    valid_candidates.insert(std::make_pair(latency, std::make_pair(the_beacon, self_egress_if_no)));
                }
            }
        }

        int no_beacons = 0;
        for (auto const & latency_beacon_pair : valid_candidates) {
            if (no_beacons >= MAX_BEACONS_TO_SEND) {
                break;
            }
            no_beacons++;

            ld  latency = latency_beacon_pair.first;
            beacon* the_beacon = latency_beacon_pair.second.first;
            uint16_t self_egress_if_no = latency_beacon_pair.second.second;
            uint16_t remote_ingress_if_no = node->GetRemoteAsInfo(self_egress_if_no).first;
            Ptr<SCION_Node> remote_as = node->GetRemoteAsInfo(self_egress_if_no).second;

            ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(self_egress_if_no)
                     ? (ld) node->inter_as_bwds.at(self_egress_if_no)
                     : the_beacon->bwd_stat;

            latency_map_to_beacon_and_metadata.insert(std::make_pair(latency, std::tuple<beacon *, uint16_t, uint16_t, Ptr<SCION_Node>, ld, ld>
                    (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                     latency, bwd)));

        }

        return latency_map_to_beacon_and_metadata;
    }

    void LatencyOptimized::insert_to_beacons_per_dst_sorted_by_latency(uint16_t dst_as,  beacon* the_beacon) {
        ld  latency = the_beacon->latency_stat;
        assert(beacons_per_dst_sorted_by_latency.size() > dst_as);
        if (beacons_per_dst_sorted_by_latency.at(dst_as).find(latency) == beacons_per_dst_sorted_by_latency.at(dst_as).end())
        {
            beacons_per_dst_sorted_by_latency.at(dst_as).insert(std::make_pair(latency, std::set<beacon*>()));
        }
        beacons_per_dst_sorted_by_latency.at(dst_as).at(latency).insert(the_beacon);
    }

    void LatencyOptimized::delete_from_beacons_per_dst_sorted_by_latency(uint16_t dst_as, beacon* the_beacon) {
        ld  latency = the_beacon->latency_stat;
        assert(beacons_per_dst_sorted_by_latency.size() > dst_as);
        assert(beacons_per_dst_sorted_by_latency.at(dst_as).find(latency) != beacons_per_dst_sorted_by_latency.at(dst_as).end());
        assert(beacons_per_dst_sorted_by_latency.at(dst_as).at(latency).find(the_beacon) != beacons_per_dst_sorted_by_latency.at(dst_as).at(latency).end());

        beacons_per_dst_sorted_by_latency.at(dst_as).at(latency).erase(the_beacon);
        if (beacons_per_dst_sorted_by_latency.at(dst_as).at(latency).empty())
        {
            beacons_per_dst_sorted_by_latency.at(dst_as).erase(latency);
        }
    }


}