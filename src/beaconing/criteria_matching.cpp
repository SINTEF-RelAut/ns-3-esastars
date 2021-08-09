/**
 * @file criteria_matching.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see criteria_matching.h
 * @brief Implements the specialized functions for the criteria matching beaconServer.
 */
#include<omp.h>
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/beaconing/criteria_matching.h"
#include "src/SCION/headers/utils.h"
#include "ns3/point-to-point-channel.h"

/**
 * Iterates over all the beacons for all the neighbours of the node. Finds the beacons with the highest scores (as many as
 * FIXED_BEACONS_NUMBER_TO_SEND, based on the latency and bandwidth stats) which do not generate loops and disseminates those
 * along the appropriate interfaces.
 *
 * @see GenerateBeaconAndSend
 * @param valid_interfaces The interfaces along which to disseminate beacons for this type of node.
 * @param node The node which is disseminating beacons.
 */
namespace ns3 {

    void CriteriaMatching::DoInitializations(uint32_t all_nodes) {
        sent_beacons.resize (node->GetNDevices ());
        sent_beacons_cnt.resize(all_nodes);

        for (uint32_t i = 0; i < node->GetNDevices (); ++i)
        {
            sent_beacons.at (i) = new std::unordered_map<Beacon *, std::pair<float, uint16_t>> ();
        }

        for (uint16_t i = 0; i < (uint16_t) all_nodes; ++i) {
            sent_beacons_cnt.at(i) = new std::unordered_map<uint16_t, uint16_t> ();
            for (auto const & neighbor_ifaces_pair : node->interfaces_per_neighbor_as) {
                uint16_t neighbor = neighbor_ifaces_pair.first;
                sent_beacons_cnt.at(i)->insert(std::make_pair(neighbor, 0));
            }
        }

        for (uint32_t i = 0; i < node->neighbors.size (); ++i)
        {
            uint16_t neighbor_as_no = node->neighbors.at(i).first;
            links_jointnesses_on_sent_paths.insert (std::make_pair (
                    neighbor_as_no, std::vector<std::unordered_map<uint32_t, uint32_t> *> ()));
            links_jointnesses_on_sent_paths.at (neighbor_as_no).resize (all_nodes);
            links_jointnesses_on_received_paths.resize(all_nodes);
            for (uint32_t j = 0; j < all_nodes; ++j)
            {
                links_jointnesses_on_sent_paths.at (neighbor_as_no).at (j) =
                        new std::unordered_map<uint32_t, uint32_t> ();
                links_jointnesses_on_received_paths.at(j) = new std::unordered_map<uint32_t, uint32_t> ();
            }
        }
    }

    void
    CriteriaMatching::DisseminateBeacons(neighbour_relation relation) {
        uint32_t neighbors_cnt = node->neighbors.size();
        omp_set_num_threads(NUM_CORE);
#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) { // Per neighbor AS

            if (node->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t remote_as_no = node->neighbors.at(i).first;
            for (auto const &dst_as_beacons_pair : beacon_store) { // Per destination AS
                uint16_t dst_as_no = dst_as_beacons_pair.first;
                const beacons_with_same_dst_as &beacons_to_the_dst_as = dst_as_beacons_pair.second;

                if (remote_as_no == dst_as_no) {
                    continue;
                }

                std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, ld, ld> > selected_beacons =
                        select_beacons_to_disseminate_per_dst_per_nbr(remote_as_no, dst_as_no, beacons_to_the_dst_as);

                for (auto const &the_tuple_pair : selected_beacons) {
                    Beacon *the_beacon;
                    uint16_t remote_ingress_if_no;
                    uint16_t self_egress_if_no;
                    SCION_AS* remote_as;
                    ld latency;
                    ld bwd;

                    std::tie(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as, latency,
                             bwd) = the_tuple_pair.second;

                    GenerateBeaconAndSend(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                                latency, bwd);

                }
            }
        }

    }


    std::tuple<bool, bool, bool, Beacon*> CriteriaMatching::ImportPolicy (Beacon& the_beacon,
                                                                          uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                                                                          uint16_t now)
    {

        uint16_t dst_as = UPPER_16_BITS(the_beacon.the_path.at(0));

        if (path_map_to_beacon.find(the_beacon.key) != path_map_to_beacon.end()) {
            Beacon* existing_beacon = path_map_to_beacon.at(the_beacon.key);
            if (!existing_beacon->is_valid){
                return std::tuple<bool, bool, bool, Beacon*>(true, true, false, existing_beacon);
            }
            return std::tuple<bool, bool, bool, Beacon*>(true, true, true, existing_beacon);
        }

        if (the_beacon.the_path.size() == 1) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        if (next_round_valid_beacons_count_per_dst_as.find(dst_as) == next_round_valid_beacons_count_per_dst_as.end()) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        if (next_round_valid_beacons_count_per_dst_as.at(dst_as) < MAX_BEACONS_TO_STORE) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        ld raw_score = calculate_import_raw_score (the_beacon);
        ld beacon_age = (ld) (now - the_beacon.next_initiation_time);
        ld beacon_exp_period = (ld) (the_beacon.next_expiration_time - the_beacon.next_initiation_time);
        ld score = std::pow(raw_score, ALPHA * (beacon_age / beacon_exp_period));

        if (next_round_valid_beacons_count_per_dst_as.at(dst_as) < MAX_BEACONS_TO_STORE && score > 0.9) {
            return std::tuple<bool, bool, bool, Beacon*>(true, false, false, NULL);
        }

        return std::tuple<bool, bool, bool, Beacon*>(false, false, false, NULL);
    }

    void
    CriteriaMatching::DeleteFromStrategyMetaData (Beacon* the_beacon) {
        dec_links_jointnesses_on_received_paths(the_beacon, UPPER_16_BITS(the_beacon->the_path.at(0)));
    }

    void CriteriaMatching::InsertToStrategyMetaData (Beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) {
        inc_links_jointness_on_received_paths(UPPER_16_BITS(the_beacon->the_path.at(0)), the_beacon);
    }




    void
    CriteriaMatching::MetaDataUpdatePeriodic (Beacon* the_beacon, bool invalidated) {
        uint16_t dst_as = UPPER_16_BITS(the_beacon->the_path.at(0));
        remove_invalid_sent_beacons(the_beacon, dst_as);

        if (invalidated) {
            dec_links_jointnesses_on_received_paths(the_beacon, dst_as);
        }
    }


    std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, ld, ld> >
    CriteriaMatching::select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                                    const beacons_with_same_dst_as &beacons_to_the_dst_as) {
        std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, ld, ld> > score_map_to_beacon_and_metadata;
        std::map<std::pair<Beacon *, uint16_t>, std::pair<ld, ld> > valid_candidates;

        ld max_score = 0.0;
        ld max_score_raw_score = 0.0;
        Beacon *max_score_beacon = NULL;
        uint16_t max_score_iface = 0;

        uint16_t remote_ingress_if_no;
        SCION_AS* remote_as = node->GetRemoteAsInfo(
                node->interfaces_per_neighbor_as.at(remote_as_no).at(0)).second;

        uint32_t min_no_paths_to_send = (20 * node->interfaces_per_neighbor_as.at(remote_as_no).size()) / remote_as->interfaces_coordinates.size();

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
                    ld raw_score = 0.0;
                    ld score = 0.0;
                    bool must_be_added = false;
                    if (path_not_sent_before(remote_as_no, self_egress_if_no, the_beacon)) {
                        raw_score = calculate_raw_score(the_beacon, dst_as_no, self_egress_if_no, remote_as);
                        ld beacon_age = (ld) (now - the_beacon->initiation_time);
                        ld beacon_exp_period = (ld) (the_beacon->expiration_time - the_beacon->initiation_time);
                        score = std::pow(raw_score, ALPHA * (beacon_age / beacon_exp_period));

                        if (sent_beacons_cnt.at(dst_as_no)->at(remote_as_no) < min_no_paths_to_send && valid_candidates.size() < min_no_paths_to_send) {
                            must_be_added = true;
                        }

                    } else {
                        raw_score = sent_beacons.at(self_egress_if_no)->at(the_beacon).first;
                        ld sent_beacon_time_to_expiration = (ld) (
                                sent_beacons.at(self_egress_if_no)->at(the_beacon).second - now);
                        ld current_beacon_time_to_expiration = (ld) (the_beacon->expiration_time - now);
                        score = std::pow(raw_score,
                                         std::pow(BETA *
                                                  (sent_beacon_time_to_expiration / current_beacon_time_to_expiration),
                                                  GAMMA));
                    }

                    if (!must_be_added && score < SCORE_THRESHOLD) {
                        continue;
                    }

                    if (score > max_score) {
                        max_score = score;
                        max_score_raw_score = raw_score;
                        max_score_beacon = the_beacon;
                        max_score_iface = self_egress_if_no;
                    }

                    valid_candidates.insert(std::make_pair(std::make_pair(the_beacon, self_egress_if_no),
                                                           std::make_pair(raw_score, score)));
                }
            }
        }

        bool counters_changed = false;

        for (int i = 0; i < MAX_BEACONS_TO_SEND; ++i) {
            if (max_score_beacon == NULL) {
                break;
            } else {
                valid_candidates.erase(std::make_pair(max_score_beacon, max_score_iface));

                remote_ingress_if_no = node->GetRemoteAsInfo(max_score_iface).first;

                ld latency = max_score_beacon->latency_stat +
                             node->latencies_between_interfaces.at(LOWER_16_BITS(max_score_beacon->the_path.back())).at(
                                     max_score_iface);
                ld bwd = max_score_beacon->bwd_stat > (ld) node->inter_as_bwds.at(max_score_iface)
                         ? (ld) node->inter_as_bwds.at(max_score_iface)
                         : max_score_beacon->bwd_stat;

                score_map_to_beacon_and_metadata.insert(
                        std::make_pair(max_score,
                                       std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, ld, ld>
                                               (max_score_beacon, max_score_iface, remote_ingress_if_no, remote_as,
                                                latency, bwd)));

                if (path_not_sent_before(remote_as_no, max_score_iface, max_score_beacon)) {
                    counters_changed = true;
                    add_to_sent_beacons(dst_as_no, remote_as_no, max_score_iface, max_score_beacon, (float) max_score_raw_score);
                    inc_links_jointness_on_sent_paths(dst_as_no, remote_as_no, max_score_iface, max_score_beacon);
                } else {
                    counters_changed = false;
                    update_sent_beacon_timer(remote_as_no, max_score_iface, max_score_beacon);
                }
            }

            max_score = 0.0;
            max_score_raw_score = 0.0;
            max_score_beacon = NULL;
            max_score_iface = 0;

            for (auto const &candidate : valid_candidates) {
                Beacon *the_beacon = candidate.first.first;
                uint16_t self_egress_if_no = candidate.first.second;
                ld score = 0.0;
                ld raw_score = 0.0;

                bool must_be_sent = path_not_sent_before(remote_as_no, self_egress_if_no, the_beacon) &&
                        sent_beacons_cnt.at(dst_as_no)->at(remote_as_no) < min_no_paths_to_send;

                if (!counters_changed || !path_not_sent_before(remote_as_no, self_egress_if_no, the_beacon)) {
                    raw_score = candidate.second.first;
                    score = candidate.second.second;
                } else {
                    raw_score = calculate_raw_score(the_beacon, dst_as_no, self_egress_if_no, remote_as);
                    ld beacon_age = (ld) (now - the_beacon->initiation_time);
                    ld beacon_exp_period = (ld) (the_beacon->expiration_time - the_beacon->initiation_time);
                    score = std::pow(raw_score, ALPHA * (beacon_age / beacon_exp_period));
                    valid_candidates.at(std::make_pair(the_beacon, self_egress_if_no)) = std::make_pair(raw_score,
                                                                                                        score);
                }

                if (!must_be_sent && score < SCORE_THRESHOLD) {
                    continue;
                }

                if (score > max_score) {
                    max_score = score;
                    max_score_raw_score = raw_score;
                    max_score_beacon = the_beacon;
                    max_score_iface = self_egress_if_no;
                }
            }
        }

        return score_map_to_beacon_and_metadata;
    }

    inline ld
    CriteriaMatching::calculate_raw_score (Beacon* the_beacon, uint16_t dst_as_no, uint16_t self_egress_if_no, SCION_AS* remote_as) {
        ld latency = the_beacon->latency_stat +
                     node->latencies_between_interfaces.at(LOWER_16_BITS(the_beacon->the_path.back())).at(self_egress_if_no);
        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(self_egress_if_no)
                 ? (ld) node->inter_as_bwds.at(self_egress_if_no)
                 : the_beacon->bwd_stat;


        ld link_diversity_score = calculate_link_diversity_score_for_dissemination(
                remote_as->as_number, dst_as_no, self_egress_if_no, the_beacon);

        ld  raw_score =
                    (
                        (1 - latency / MAX_LAT) * ((ns3::CriteriaMatching*) remote_as->GetBeaconServer())->latency_coef +
                        (bwd / MAX_BWD) * ((ns3::CriteriaMatching*) remote_as->GetBeaconServer())->bandwidth_coef +
                        link_diversity_score * ((ns3::CriteriaMatching*) remote_as->GetBeaconServer())->link_level_diversity_coef
                    )
                    /
                    (
                            ((ns3::CriteriaMatching*) remote_as->GetBeaconServer())->latency_coef +
                            ((ns3::CriteriaMatching*) remote_as->GetBeaconServer())->bandwidth_coef +
                            ((ns3::CriteriaMatching*) remote_as->GetBeaconServer())->link_level_diversity_coef
                    );

        raw_score = SCALING_FACTOR * raw_score;

        return raw_score;
    }


    inline ld
    CriteriaMatching::calculate_import_raw_score (Beacon& the_beacon) {
        uint16_t dst_as_no = UPPER_16_BITS (the_beacon.the_path.at(0));
        ld  latency = the_beacon.latency_stat;
        ld  bwd = the_beacon.bwd_stat;

        ld link_diversity_score = calculate_link_diversity_score_for_import(dst_as_no, the_beacon);

        ld  raw_score =
                (
                        (1 - latency / MAX_LAT) * this->latency_coef +
                        (bwd / MAX_BWD) * this->bandwidth_coef +
                        link_diversity_score * this->link_level_diversity_coef
                )
                /
                (
                        this->latency_coef +
                        this->bandwidth_coef +
                        this->link_level_diversity_coef
                );

        raw_score = SCALING_FACTOR * raw_score;

        return raw_score;
    }

    void
    CriteriaMatching::update_sent_beacon_timer(uint16_t remote_as, uint16_t self_egress_if_no, Beacon *the_beacon) {
        uint16_t new_exp_time = the_beacon->expiration_time;
        float raw_score = sent_beacons.at(self_egress_if_no)->at(the_beacon).first;
        sent_beacons.at(self_egress_if_no)->at(the_beacon) = std::make_pair(raw_score, new_exp_time);
    }

    void
    CriteriaMatching::inc_links_jointness_on_sent_paths(uint16_t dst_as_no, uint16_t remote_as_no,
                                                        uint16_t self_egress_if_no, Beacon *the_beacon) {
        if (the_beacon != NULL) {
            auto const & the_path = the_beacon->the_path;
            for (auto const &seg : the_path) {
                uint32_t link = UPPER_32_BITS(seg);
                if (links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->find(link) ==
                    links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->end()) {
                    links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->insert(std::make_pair(link, 0));
                }
                links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->at(link) =
                        links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->at(link) + 1;
            }
        }

        uint32_t link = (((uint32_t) node->as_number) << 16) | ((uint32_t) self_egress_if_no);
        if (links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->find(link) ==
            links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->end()) {
            links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->insert(std::make_pair(link, 0));
        }
        links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->at(link) =
                links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as_no)->at(link) + 1;

    }

    void
    CriteriaMatching::inc_links_jointness_on_received_paths(uint16_t dst_as_no, Beacon *the_beacon) {

        auto const & the_path = the_beacon->the_path;
        for (auto const &seg : the_path) {
            uint32_t link = UPPER_32_BITS(seg);
            if (links_jointnesses_on_received_paths.at(dst_as_no)->find(link) ==
                links_jointnesses_on_received_paths.at(dst_as_no)->end()) {
                links_jointnesses_on_received_paths.at(dst_as_no)->insert(std::make_pair(link, 0));
            }
            links_jointnesses_on_received_paths.at(dst_as_no)->at(link) = links_jointnesses_on_received_paths.at(dst_as_no)->at(link) + 1;
        }
    }

    void
    CriteriaMatching::add_to_sent_beacons(uint16_t dst_as_no, uint16_t remote_as, uint16_t self_egress_if_no, Beacon *the_beacon,
                                          float raw_score) {
        sent_beacons.at(self_egress_if_no)->insert(
                std::make_pair(the_beacon, std::make_pair(raw_score, the_beacon->expiration_time)));
        sent_beacons_cnt.at(dst_as_no)->at(remote_as)++;
    }

    ld
    CriteriaMatching::calculate_link_diversity_score_for_dissemination(uint16_t remote_as, uint16_t dst_as,
                                                                       uint16_t egress_if_no, Beacon *the_beacon) {
        if (links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->empty()) {
            return 1.0;
        }

        ld add_one = path_not_sent_before(remote_as, egress_if_no, the_beacon) ? 1.0 : 0.0;

        ld jointness = 1.0;
        auto const & the_path = the_beacon->the_path;
        for (auto const &seg : the_path) {
            uint32_t link = UPPER_32_BITS(seg);
            if (links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->find(link) !=
                links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->end()) {
                jointness *= (add_one + 1.0 * links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->at(link));
            }
        }

        uint32_t link = (((uint32_t) node->as_number) << 16) | ((uint32_t) egress_if_no);
        if (links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->find(link) !=
            links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->end()) {
            jointness *= (add_one + 1.0 * links_jointnesses_on_sent_paths.at(remote_as).at(dst_as)->at(link));
        }

        jointness = std::pow(jointness, 1.0 / (the_beacon->the_path.size() + 1.0));

        if (jointness >= MAX_ACCEPTABLE_JOINTNESS) {
            return 0.0;
        }
        return (MAX_ACCEPTABLE_JOINTNESS - jointness) / (MAX_ACCEPTABLE_JOINTNESS - 1.0);
    }

    ld
    CriteriaMatching::calculate_link_diversity_score_for_import(uint16_t dst_as, Beacon& the_beacon) {
        if (links_jointnesses_on_received_paths.at(dst_as)->empty()) {
            return 1.0;
        }

        ld jointness = 1.0;
        auto const & the_path = the_beacon.the_path;
        for (auto const &seg : the_path) {
            uint32_t link = UPPER_32_BITS(seg);
            if (links_jointnesses_on_received_paths.at(dst_as)->find(link) !=
                    links_jointnesses_on_received_paths.at(dst_as)->end()) {
                jointness *= (1.0 + 1.0 * links_jointnesses_on_received_paths.at(dst_as)->at(link));
            }
        }

        jointness = std::pow(jointness, 1.0 / (the_beacon.the_path.size() + 1.0));

        if (jointness >= MAX_ACCEPTABLE_JOINTNESS) {
            return 0.0;
        }
        return (MAX_ACCEPTABLE_JOINTNESS - jointness) / (MAX_ACCEPTABLE_JOINTNESS - 1.0);
    }
    bool
    CriteriaMatching::path_not_sent_before(uint16_t remote_as, uint16_t self_egress_if_no, Beacon *the_beacon) {
        if (sent_beacons.at(self_egress_if_no)->find(the_beacon) == sent_beacons.at(self_egress_if_no)->end()) {
            return true;
        }
        return false;
    }


    void
    CriteriaMatching::remove_invalid_sent_beacons(Beacon* the_beacon, uint16_t dst_as) {
        for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
            uint16_t remote_as_no = node->interface_to_neighbor_map.at(i);
            if (sent_beacons.at(i)->find(the_beacon) == sent_beacons.at(i)->end()) {
                continue;
            }

            if (sent_beacons.at(i)->at(the_beacon).second <= next_period) {
                sent_beacons.at(i)->erase(the_beacon);
                sent_beacons_cnt.at(dst_as)->at(node->interface_to_neighbor_map.at(i))--;
                dec_links_jointnesses_on_sent_paths(the_beacon, dst_as, remote_as_no, i);
            }


        }
    }

    void
    CriteriaMatching::dec_links_jointnesses_on_sent_paths(Beacon* the_beacon, uint16_t  dst_as, uint16_t remote_as_no, uint16_t self_egress_if) {
        auto const & the_path = the_beacon->the_path;
        for (auto const & seg : the_path) {
            uint32_t link = UPPER_32_BITS(seg);
            links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->at(link) = links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->at(link) - 1;
            if (links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->at(link) == 0) {
                links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->erase(link);
            }
        }

        uint32_t link = (((uint32_t) node->as_number) << 16) | ((uint32_t) self_egress_if);

        links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->at(link) = links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->at(link) - 1;
        if (links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->at(link) == 0) {
            links_jointnesses_on_sent_paths.at(remote_as_no).at(dst_as)->erase(link);
        }
    }

    void
    CriteriaMatching::dec_links_jointnesses_on_received_paths(Beacon* the_beacon, uint16_t  dst_as) {
        auto const & the_path = the_beacon->the_path;
        for (auto const & seg : the_path) {
            uint32_t link = UPPER_32_BITS(seg);
            links_jointnesses_on_received_paths.at(dst_as)->at(link) = links_jointnesses_on_received_paths.at(dst_as)->at(link) - 1;
            if (links_jointnesses_on_received_paths.at(dst_as)->at(link) == 0) {
                links_jointnesses_on_received_paths.at(dst_as)->erase(link);
            }
        }
    }
}





