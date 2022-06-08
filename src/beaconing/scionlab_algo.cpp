/**
 * @file scionlab_algo.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see scionlab_algo.h
 */

#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "ns3/point-to-point-channel.h"
#include "src/SCION/headers/utils.h"
#include <omp.h>

namespace ns3 {

    void SCIONLAB::DoInitializations(uint32_t num_ASes, rapidxml::xml_node<> *xml_node, const YAML::Node &config) {
        BeaconServer::DoInitializations(num_ASes, xml_node, config);
    }

    void SCIONLAB::create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                        uint16_t self_egress_if_no,
                                                        const optimization_target_t *optimization_target) {
        static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, 0));
        static_info_extension.insert(std::make_pair(static_info_type_t::BW, AS->inter_as_bwds.at(self_egress_if_no)));
    }

    void SCIONLAB::disseminate_beacons(neighbour_relation relation) {
        uint32_t neighbors_cnt = AS->neighbors.size();
        std::vector<Beacon *> valid_candidates;

        for (auto const &dst_as_beacons_pair : beacon_store) {
            auto const &equal_dst_as_beacons = dst_as_beacons_pair.second;

            std::vector<Beacon *> rest_of_beacons;
            std::vector<Beacon *> selected_beacons_per_dst;

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

            std::pair<Beacon *, int32_t> diversity_wr_to_selected =
                    select_most_diverse(selected_beacons_per_dst, selected_beacons_per_dst.at(0));
            std::pair<Beacon *, int32_t> diversity_wr_to_rest =
                    select_most_diverse(rest_of_beacons, selected_beacons_per_dst.at(0));

            if (diversity_wr_to_rest.second > diversity_wr_to_selected.second) {
                valid_candidates.push_back(diversity_wr_to_rest.first);
            } else {
                valid_candidates.push_back(rest_of_beacons.at(0));
            }
        }

        omp_set_num_threads(NUM_CORE);
#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) {
            if (AS->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t &remote_as_no = AS->neighbors.at(i).first;
            const std::vector<uint16_t> &interfaces = AS->interfaces_per_neighbor_as.at(remote_as_no);

            for (auto const &the_beacon : valid_candidates) {
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
                uint16_t remote_isd_no = AS->GetRemoteAsInfo(interfaces.back()).second->isd_number;
                if (remote_isd_no != AS->isd_number) {
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
                    std::pair<uint16_t, SCION_AS *> remote_as_if_pair = AS->GetRemoteAsInfo(egress_interface_no);

                    uint16_t remote_ingress_if_no = remote_as_if_pair.first;
                    SCION_AS *remote_as = remote_as_if_pair.second;

                    ld latency = the_beacon->static_info_extension.at(static_info_type_t::LATENCY) +
                                 AS->latencies_between_interfaces.at(LOWER_16_BITS(the_beacon->the_path.back()))
                                         .at(egress_interface_no);
                    ld bwd = the_beacon->static_info_extension.at(static_info_type_t::BW) >
                                             (ld) AS->inter_as_bwds.at(egress_interface_no)
                                     ? (ld) AS->inter_as_bwds.at(egress_interface_no)
                                     : the_beacon->static_info_extension.at(static_info_type_t::BW);

                    static_info_extension_t static_info_extension;
                    static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, latency));
                    static_info_extension.insert(std::make_pair(static_info_type_t::BW, bwd));

                    generate_beacon_and_send(the_beacon, egress_interface_no, remote_ingress_if_no, remote_as,
                                             static_info_extension);
                }
            }
        }
    }

    std::tuple<bool, bool, bool, Beacon *, ld>
    SCIONLAB::alg_specific_import_policy(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                         uint16_t self_ingress_if_no, uint16_t now) {
        uint16_t dst_as = UPPER_16_BITS(the_beacon.the_path.at(0));

        if (next_round_valid_beacons_count_per_dst_as.find(dst_as) == next_round_valid_beacons_count_per_dst_as.end()) {
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, NULL, 0);
        }

        if (this->next_round_valid_beacons_count_per_dst_as.at(dst_as) < MAX_BEACONS_TO_STORE) {
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, NULL, 0);
        }

        return std::tuple<bool, bool, bool, Beacon *, ld>(false, false, false, NULL, 0);
    }

    void SCIONLAB::insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as,
                                                       uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) {}

    void SCIONLAB::delete_from_algorithm_data_structures(Beacon *the_beacon, ld replacement_key) {}

    void SCIONLAB::update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) {}

    std::pair<Beacon *, int32_t> SCIONLAB::select_most_diverse(std::vector<Beacon *> &beacons, Beacon *the_beacon) {
        if (beacons.size() == 0) {
            return std::make_pair(the_beacon, -1);
        }
        Beacon *diverse = NULL;
        int32_t max_diversity = -1;
        uint32_t min_len = std::numeric_limits<uint32_t>::max();

        for (auto const &other_beacon : beacons) {
            int32_t diversity = calc_diversity(the_beacon, other_beacon);
            uint32_t l = other_beacon->the_path.size();

            if (diversity > max_diversity || (diversity == max_diversity && min_len > l)) {
                diverse = other_beacon;
                min_len = l;
                max_diversity = diversity;
            }
        }
        return std::make_pair(diverse, max_diversity);
    }

    int32_t SCIONLAB::calc_diversity(Beacon *beacon1, Beacon *beacon2) {
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
