//
// Created by seyedali on 09.07.21.
//

#include <omp.h>
#include "src/SCION/headers/utils.h"
#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "ns3/point-to-point-channel.h"

namespace ns3 {
    void SCIONLAB::DoInitializations(uint32_t all_nodes) {

    }


/**
 * Iterates over all the beacons for all the neighbours of the node. If the beacon is valid, its dissemination towards
 * this neighbour does not create a loop in the path and the neighbour is not the same AS which originated the beacon
 * it is sent over the interfaces towards this neighbour until the maximum number of beacons to send per neighbour has
 * been reached.
 *
 * @see GenerateBeaconAndSend
 * @param valid_interfaces The interfaces along which to disseminate beacons for this type of node.
 * @param node The node which is disseminating beacons.
 */
    void
    SCIONLAB::DisseminateBeacons (neighbour_relation relation)
    {
        uint32_t neighbors_cnt = node->neighbors.size ();
        std::vector<beacon*> valid_candidates;

        for (auto const &dst_as_beacons_pair : beacon_store) {
            auto const & equal_dst_as_beacons = dst_as_beacons_pair.second;

            std::vector<beacon*> rest_of_beacons;
            std::vector<beacon*> selected_beacons_per_dst;

            for (auto const &len_beacons_pair : equal_dst_as_beacons) { // for each length
                if (rest_of_beacons.size() + selected_beacons_per_dst.size() >= MAX_SET_SIZE) {
                    break;
                }

                auto const &beacons = len_beacons_pair.second;
                for (auto const &the_beacon : beacons) {
                    if (rest_of_beacons.size() + selected_beacons_per_dst.size() >= MAX_SET_SIZE) {
                        break;
                    }

                    if (!the_beacon->is_valid) {
                        continue;
                    }

                    if (selected_beacons_per_dst.size() < MAX_BEACONS_TO_SEND - 1) {
                        valid_candidates.push_back(the_beacon);
                        selected_beacons_per_dst.push_back(the_beacon);
                    } else {
                        rest_of_beacons.push_back(the_beacon);
                    }
                }
            }



            if (rest_of_beacons.size() + selected_beacons_per_dst.size() == MAX_BEACONS_TO_SEND) {
                valid_candidates.push_back(rest_of_beacons.at(0));
                continue;
            }

            if (rest_of_beacons.size() + selected_beacons_per_dst.size() < MAX_BEACONS_TO_SEND) {
                continue;
            }

            std::pair<beacon*, int32_t> diversity_wr_to_selected = SelectMostDiverse(selected_beacons_per_dst, selected_beacons_per_dst.at(0));

            std::pair<beacon*, int32_t> diversity_wr_to_rest = SelectMostDiverse(rest_of_beacons, selected_beacons_per_dst.at(0));

            if (diversity_wr_to_rest.second > diversity_wr_to_selected.second) {
                valid_candidates.push_back(diversity_wr_to_rest.first);
            } else {
                valid_candidates.push_back(rest_of_beacons.at(0));
            }

        }

        omp_set_num_threads (NUM_CORE);
#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) {
            if (node->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t &remote_as_no = node->neighbors.at(i).first;
            const std::vector<uint16_t> &interfaces = node->interfaces_per_neighbor_as.at(remote_as_no);

            for (auto const & the_beacon : valid_candidates) {
                uint16_t dst_as_no = UPPER_16_BITS(the_beacon->the_path.at(0));

                if (remote_as_no == dst_as_no) {
                    continue;
                }

                bool generates_loop = false;

                for (auto const &link_info : the_beacon->the_path) { // filter AS loops
                    if (UPPER_16_BITS(link_info) == remote_as_no) {
                        generates_loop = true;
                        break;
                    }
                }

                if (generates_loop) {
                    continue;
                }

                // filter isd loops
                uint16_t remote_isd_no = node->GetRemoteAsInfo(interfaces.back()).second->isd_number;
                if (remote_isd_no != node->isd_number) {
                    for (uint16_t isd_number : the_beacon->the_isd_path) {
                        if (isd_number == remote_isd_no) {
                            generates_loop = true;
                            break;
                        }
                    }
                }

                if (generates_loop) {
                    continue;
                }

                // Iterate over all the valid interfaces of this remote AS and send the beacons
                for (auto const &egress_interface_no : interfaces) {


                    std::pair<uint16_t, Ptr<SCION_AS>>
                            remote_as_if_pair = node->GetRemoteAsInfo(egress_interface_no);

                    uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                    Ptr<SCION_AS> remote_as = remote_as_if_pair.second;

                    ld latency = the_beacon->latency_stat +
                                 node->intra_as_latencies
                                         .at(LOWER_16_BITS(the_beacon->the_path.back()))
                                         .at(egress_interface_no);
                    ld bwd =
                            the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(
                                    egress_interface_no)
                            ? (ld) node->inter_as_bwds.at(
                                    egress_interface_no)
                            : the_beacon->bwd_stat;

                    GenerateBeaconAndSend(the_beacon, egress_interface_no,
                                          remote_ingress_if_no,
                                          remote_as, latency, bwd);
                }


            }
        }
    }

/**
 * @param key Beacon key.
 * @param dst_as The AS number of the node which originated the beacon.
 * @param old_beacon The previous beacon.
 * @param self_egress_if_no The interface number on which to send the beacon.
 * @param remote_ingress_if_no The interface number where the beacon will be received on the remote_as.
 * @param node The node sending the beacon.
 * @param remote_as The node receiving the beacon.
 * @param latency The new beacon latency.
 * @param bwd The new beacon bandwidth stat.
 */
    std::tuple<bool, bool, bool, beacon*>
    SCIONLAB::ImportPolicy(beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                           uint16_t self_ingress_if_no, uint16_t now)
    {
        uint16_t dst_as = UPPER_16_BITS(the_beacon.the_path.at(0));

        if (path_map_to_beacon.find(the_beacon.key) != path_map_to_beacon.end()) {
            beacon* existing_beacon = path_map_to_beacon.at(the_beacon.key);
            if (!existing_beacon->is_valid){
                return std::tuple<bool, bool, bool, beacon*>(true, true, false, existing_beacon);
            }
            return std::tuple<bool, bool, bool, beacon*>(true, true, true, existing_beacon);
        }

        if (the_beacon.the_path.size() == 1) {
            return std::tuple<bool, bool, bool, beacon*>(true, false, false, NULL);
        }

        if (next_round_valid_beacons_count_per_dst_as.find(dst_as) == next_round_valid_beacons_count_per_dst_as.end()) {
            return std::tuple<bool, bool, bool, beacon*>(true, false, false, NULL);
        }

        if (this->next_round_valid_beacons_count_per_dst_as.at(dst_as) < MAX_BEACONS_TO_STORE) {
            return std::tuple<bool, bool, bool, beacon*>(true, false, false, NULL);
        }

        return std::tuple<bool, bool, bool, beacon*>(false, false, false, NULL);
    }

    void
    SCIONLAB::InsertToStrategyMetaData (beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no){}

    void
    SCIONLAB::DeleteFromStrategyMetaData (beacon* the_beacon) {}


    void
    SCIONLAB::MetaDataUpdatePeriodic (beacon* the_beacon, bool invalidated)
    {
    }


    std::pair<beacon*, int32_t>
    SCIONLAB::SelectMostDiverse (std::vector<beacon*>& beacons, beacon* the_beacon) {
        if (beacons.size() == 0) {
            return std::make_pair(the_beacon, -1);
        }
        beacon* diverse;
        int32_t max_diversity = -1;
        uint32_t min_len = std::numeric_limits<uint32_t>::max();

        for (auto const & other_beacon : beacons) {
            int32_t diversity = Diversity(the_beacon, other_beacon);
            uint32_t l = other_beacon->the_path.size();

            if (diversity > max_diversity || (diversity == max_diversity && min_len > l)) {
                diverse = other_beacon;
                min_len = l;
                max_diversity = diversity;
            }
        }

        return std::make_pair(diverse, max_diversity);
    }

    int32_t
    SCIONLAB::Diversity (beacon* beacon1, beacon* beacon2) {
        int32_t diff = 0;

        for (uint64_t link_info : beacon1->the_path) {
            bool found = false;
            for (uint64_t other_link_info : beacon2->the_path) {
                if (link_info == other_link_info) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                diff++;
            }
        }

        return diff;
    }

} // namespace ns3

