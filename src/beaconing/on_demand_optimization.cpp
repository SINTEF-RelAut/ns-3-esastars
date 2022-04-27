//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#include "src/SCION/headers/beaconing/on_demand_optimization.h"
#include "src/SCION/headers/utils.h"
#include <omp.h>

namespace ns3 {

    void OnDemandOptimization::DoInitializations(uint32_t num_ASes, rapidxml::xml_node<> *xml_node,
                                                 const YAML::Node &config) {
        rapidxml::xml_node<> *cur_xml_node = xml_node->first_node("node");
        uint16_t alias_as_number = 0;
        while (cur_xml_node) {
            if (alias_as_number == AS->as_number) {
                rapidxml::xml_node<> *cur_target = cur_xml_node->first_node("target");
                while (cur_target) {
                    uint16_t target_id = std::stoi(cur_target->first_node("target_id")->value());

                    optimization_criteria_t optimization_criteria;
                    PropertyContainer p = parseProperties(cur_target);
                    if (p.hasProperty("bw") && std::stof(p.getProperty("bw")) > 0.001) {
                        optimization_criteria.insert(std::make_pair(static_info_type_t::BW, std::stof(p.getProperty("bw"))));
                    }

                    if (p.hasProperty("latency") && std::stof(p.getProperty("latency")) > 0.001) {
                        optimization_criteria.insert(std::make_pair(static_info_type_t::LATENCY, std::stof(p.getProperty("latency"))));
                    }

                    std::string direction = cur_target->first_node("direction")->value();
                    optimization_direction_t optimization_direction;
                    if (direction == "forward") {
                        optimization_direction = optimization_direction_t::FORWARD;
                    } else if (direction == "backward") {
                        optimization_direction = optimization_direction_t::BACKWARD;
                    } else if (direction == "symmetric") {
                        optimization_direction = optimization_direction_t::SYMMETRIC;
                    }

                    uint16_t group_id = std::stoi(cur_target->first_node("group_id")->value());
                    uint16_t no_beacons = std::stoi(cur_target->first_node("no_beacons")->value());

                    set_of_optimization_targets_originated_from_this_as.insert(
                            std::make_pair(target_id, optimization_target_t(optimization_criteria,
                                                                            optimization_direction,
                                                                            alias_as_number,
                                                                            group_id,
                                                                            no_beacons,
                                                                            NULL)));
                    cur_target = cur_target->next_sibling("target");
                }

                break;
            }
            alias_as_number++;
            cur_xml_node = cur_xml_node->next_sibling("node");
        }

        rapidxml::xml_node<> *cur_xml_link = xml_node->first_node("link");

        while (cur_xml_link) {
            uint32_t to = std::stoi(cur_xml_link->first_node("to")->value());
            uint32_t from = std::stoi(cur_xml_link->first_node("from")->value());

            std::string target_element_str = "_target";
            uint16_t interface_id = 0;
            uint16_t neighbor_as = 0;

            PropertyContainer p = parseProperties(cur_xml_link);
            if (real_to_alias_as_no.at(to) == AS->as_number) {
                target_element_str = "to" + target_element_str;
                interface_id = std::stoi(p.getProperty("to_if_id"));
                neighbor_as = real_to_alias_as_no.at(from);
            } else if (real_to_alias_as_no.at(from) == AS->as_number) {
                target_element_str = "from" + target_element_str;
                interface_id = std::stoi(p.getProperty("from_if_id"));
                neighbor_as = real_to_alias_as_no.at(to);
            } else {
                cur_xml_link = cur_xml_link->next_sibling("link");
                continue;
            }

            ld latitude = std::stod(p.getProperty("latitude"));
            ld longitude = std::stod(p.getProperty("longitude"));
            std::pair<ld, ld> coordinates = std::make_pair(latitude, longitude);

            assert(coordinates == AS->interfaces_coordinates.at(interface_id));

            rapidxml::xml_node<> *cur_xml_target = xml_node->first_node(target_element_str.c_str());
            while (cur_xml_target) {
                uint16_t target_id = std::stoi(cur_xml_target->value());
                const optimization_target_t* optimization_target = &set_of_optimization_targets_originated_from_this_as.at(target_id);
                uint16_t interface_group = optimization_target->target_if_group;

                if_to_optimization_targets_map.insert(std::make_pair(interface_id, optimization_target));

                if (interface_groups_connected_per_neighbor.find(neighbor_as) == interface_groups_connected_per_neighbor.end()) {
                    interface_groups_connected_per_neighbor.insert(std::make_pair(neighbor_as, std::unordered_map<uint16_t, std::vector<uint16_t>>()));
                }
                if (interface_groups_connected_per_neighbor.at(neighbor_as).find(interface_group) == interface_groups_connected_per_neighbor.at(neighbor_as).end()) {
                    interface_groups_connected_per_neighbor.at(neighbor_as).insert(std::make_pair(interface_group, std::vector<uint16_t>()));
                }
                if (std::find(std::begin(interface_groups_connected_per_neighbor.at(neighbor_as).at(interface_group)),
                              std::end(interface_groups_connected_per_neighbor.at(neighbor_as).at(interface_group)),
                              interface_id) == std::end(interface_groups_connected_per_neighbor.at(neighbor_as).at(interface_group))) {
                    interface_groups_connected_per_neighbor.at(neighbor_as).at(interface_group).push_back(interface_id);
                }


                cur_xml_target = cur_xml_target->next_sibling(target_element_str.c_str());
            }

            cur_xml_link = cur_xml_link->next_sibling("link");
        }
    }

    void OnDemandOptimization::initiate_beacons_per_interface(uint16_t self_egress_if_no, SCION_AS *remote_as,
                                                              uint16_t remote_ingress_if_no) {
        // push_based
        for (auto it = if_to_optimization_targets_map.lower_bound(self_egress_if_no);
             it != if_to_optimization_targets_map.upper_bound(self_egress_if_no); ++it) {
            static_info_extension_t static_info_extension;
            create_initial_static_info_extension(static_info_extension, self_egress_if_no, it->second);
            generate_beacon_and_send(NULL, self_egress_if_no, remote_ingress_if_no, remote_as, static_info_extension,
                                     it->second, beacon_direction_t::PUSH_BASED);
        }

        // pull-based
        // TODO
    }

    void OnDemandOptimization::create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                                    uint16_t self_egress_if_no,
                                                                    const optimization_target_t *optimization_target) {
        for (auto const &criteria : optimization_target->criteria) {
            if (criteria.first == LATENCY) {
                static_info_extension.insert(std::make_pair(static_info_type_t::LATENCY, 0));
            } else if (criteria.first == BW) {
                static_info_extension.insert(
                        std::make_pair(static_info_type_t::BW, AS->inter_as_bwds.at(self_egress_if_no)));
            } else if (criteria.first == CO2) {
                static_info_extension.insert(std::make_pair(static_info_type_t::CO2, 0));
            }
        }
    }

    void OnDemandOptimization::extend_static_info_extension(const Beacon *the_beacon, uint16_t beacon_ingress_if_no,
                                                            uint16_t candidate_egress_if_no,
                                                            static_info_extension_t &propagation_static_info) {
        for (auto const &criteria : the_beacon->optimization_target->criteria) {
            if (criteria.first == LATENCY) {
                ld latency = the_beacon->static_info_extension.at(static_info_type_t::LATENCY) +
                             AS->latencies_between_interfaces.at(beacon_ingress_if_no).at(candidate_egress_if_no);
                propagation_static_info.insert(std::make_pair(static_info_type_t::LATENCY, latency));
            } else if (criteria.first == BW) {
                ld bw = the_beacon->static_info_extension.at(static_info_type_t::BW) >
                                        (ld) AS->inter_as_bwds.at(candidate_egress_if_no)
                                ? (ld) AS->inter_as_bwds.at(candidate_egress_if_no)
                                : the_beacon->static_info_extension.at(static_info_type_t::BW);
                propagation_static_info.insert(std::make_pair(static_info_type_t::BW, bw));
            } else if (criteria.first == CO2) {
                propagation_static_info.insert(std::make_pair(static_info_type_t::CO2, 0));
            }
        }
    }

    void OnDemandOptimization::disseminate_beacons(neighbour_relation relation) {
        uint32_t neighbors_cnt = AS->neighbors.size();
        omp_set_num_threads(NUM_CORE);
#pragma omp parallel for
        for (uint32_t i = 0; i < neighbors_cnt; ++i) { // Per neighbor AS
            if (AS->neighbors.at(i).second != relation) {
                continue;
            }

            uint16_t remote_as_no = AS->neighbors.at(i).first;

            std::unordered_map<
                    uint16_t,
                    std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>,
                                  std::greater<ld>>>
                    selected_beacons;

            // push-based
            for (auto const &[optimization_target, beacons_with_the_same_opt_target] :
                 push_based_beacons_grouped_by_optimization_targets_and_ingress_if) {
                uint16_t dst_as_no = optimization_target->target_as;

                if (remote_as_no == dst_as_no) {
                    continue;
                }

                select_beacons_to_disseminate_per_target_per_nbr(remote_as_no, dst_as_no,
                                                                 beacons_with_the_same_opt_target, optimization_target,
                                                                 selected_beacons);
            }

            // pull-based
            for (auto const &[optimization_target, beacons_with_the_same_opt_target] :
                 pull_based_beacons_grouped_by_optimization_targets_and_ingress_if) {
                uint16_t dst_as_no = optimization_target->target_as;

                if (remote_as_no == dst_as_no) {
                    continue;
                }

                select_beacons_to_disseminate_per_target_per_nbr(remote_as_no, dst_as_no,
                                                                 beacons_with_the_same_opt_target, optimization_target,
                                                                 selected_beacons);
            }

            for (auto const &group_selected_beacons_pair : selected_beacons) {
                for (auto const &score_selected_beacons_pair : group_selected_beacons_pair.second) {
                    Beacon *the_beacon;
                    uint16_t remote_ingress_if_no;
                    uint16_t self_egress_if_no;
                    SCION_AS *remote_as;
                    static_info_extension_t static_info_extension;

                    std::tie(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as, static_info_extension) =
                            score_selected_beacons_pair.second;

                    generate_beacon_and_send(the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                             static_info_extension);
                }
            }
        }
        pull_based_beacons_grouped_by_optimization_targets_and_ingress_if.clear();
    }

    void OnDemandOptimization::select_beacons_to_disseminate_per_target_per_nbr(
            uint16_t remote_as_no, uint16_t dst_as_no,
            const beacons_with_the_same_opt_target_t &beacons_with_the_same_opt_target,
            const optimization_target_t *optimization_target,
            std::unordered_map<
                    uint16_t,
                    std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>,
                                  std::greater<ld>>> &selected_beacons) {
        for (auto const &[beacon_ingress_if_no, score_beacons_map] : beacons_with_the_same_opt_target) {
            for (auto const &[score, the_beacon] : score_beacons_map) {
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

                for (auto const &[group, ifaces] : interface_groups_connected_per_neighbor.at(remote_as_no)) {
                    for (auto const &candidate_egress_if_no : ifaces) {
                        if (selected_beacons.find(group) != selected_beacons.end()) {
                            selected_beacons.insert(
                                    std::make_pair(group, std::multimap<ld,
                                                                        std::tuple<Beacon *, uint16_t, uint16_t,
                                                                                   SCION_AS *, static_info_extension_t>,
                                                                        std::greater<ld>>()));
                        }

                        static_info_extension_t propagation_static_info;
                        extend_static_info_extension(the_beacon, beacon_ingress_if_no, candidate_egress_if_no,
                                                     propagation_static_info);

                        ld score = calculate_score(the_beacon->optimization_target, propagation_static_info);

                        auto [remote_ingress_if_no, remote_as] = AS->GetRemoteAsInfo(candidate_egress_if_no);

                        selected_beacons.at(group).insert(std::make_pair(
                                score, std::make_tuple(the_beacon, candidate_egress_if_no, remote_ingress_if_no,
                                                       remote_as, propagation_static_info)));
                    }
                }
            }
        }

        for (auto &subgroup_selected_beacons_per_subgroup_pair : selected_beacons) {
            auto &selected_beacons_per_subgroup = subgroup_selected_beacons_per_subgroup_pair.second;
            auto it = selected_beacons_per_subgroup.begin();
            std::advance(it, optimization_target->no_beacons_per_optimization_target);
            selected_beacons_per_subgroup.erase(it, selected_beacons_per_subgroup.end());
        }
    }

    std::tuple<bool, bool, bool, Beacon *, ld>
    OnDemandOptimization::alg_specific_import_policy(Beacon &the_beacon, uint16_t sender_as,
                                                     uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                                                     uint16_t now) {
        const auto &beacon_container = (the_beacon.beacon_direction == beacon_direction_t::PUSH_BASED)
                                               ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if
                                               : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if;

        if (beacon_container.find(the_beacon.optimization_target) == beacon_container.end()) {
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, NULL, 0);
        }

        if (beacon_container.at(the_beacon.optimization_target).find(self_ingress_if_no) ==
            beacon_container.at(the_beacon.optimization_target).end()) {
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, NULL, 0);
        }

        if (beacon_container.at(the_beacon.optimization_target).at(self_ingress_if_no).size() <
            the_beacon.optimization_target->no_beacons_per_optimization_target) {
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, NULL, 0);
        }

        std::multimap<ld, Beacon *>::const_reverse_iterator worst_beacon_score =
                beacon_container.at(the_beacon.optimization_target).at(self_ingress_if_no).rbegin();
        ld incoming_beacon_score = calculate_score(the_beacon.optimization_target, the_beacon.static_info_extension);
        ld lowest_previous_score = worst_beacon_score->first;
        if (lowest_previous_score < incoming_beacon_score) {
            Beacon *to_be_removed_beacon = worst_beacon_score->second;
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, to_be_removed_beacon,
                                                              lowest_previous_score);
        }

        return std::tuple<bool, bool, bool, Beacon *, ld>(false, false, false, NULL, 0);
    }

    void OnDemandOptimization::insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as,
                                                                   uint16_t remote_egress_if_no,
                                                                   uint16_t self_ingress_if_no) {
        auto &beacon_container = (the_beacon->beacon_direction == beacon_direction_t::PUSH_BASED)
                                         ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if
                                         : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if;

        if (beacon_container.find(the_beacon->optimization_target) == beacon_container.end()) {
            beacon_container.insert(
                    std::make_pair(the_beacon->optimization_target, beacons_with_the_same_opt_target_t()));
        }

        if (beacon_container.at(the_beacon->optimization_target).find(self_ingress_if_no) ==
            beacon_container.at(the_beacon->optimization_target).end()) {
            beacon_container.at(the_beacon->optimization_target)
                    .insert(std::make_pair(self_ingress_if_no, beacons_with_the_same_opt_target_and_ingress_if_t()));
        }

        ld incoming_beacon_score = calculate_score(the_beacon->optimization_target, the_beacon->static_info_extension);

        beacon_container.at(the_beacon->optimization_target)
                .at(self_ingress_if_no)
                .insert(std::make_pair(incoming_beacon_score, the_beacon));
    }

    void OnDemandOptimization::delete_from_algorithm_data_structures(Beacon *the_beacon, ld replacement_key) {
        uint16_t self_ingress_if = LOWER_16_BITS(the_beacon->the_path.back());

        auto &beacon_container = (the_beacon->beacon_direction == beacon_direction_t::PUSH_BASED)
                                         ? push_based_beacons_grouped_by_optimization_targets_and_ingress_if
                                                   .at(the_beacon->optimization_target)
                                                   .at(self_ingress_if)
                                         : pull_based_beacons_grouped_by_optimization_targets_and_ingress_if
                                                   .at(the_beacon->optimization_target)
                                                   .at(self_ingress_if);

        for (auto it = beacon_container.lower_bound(replacement_key);
             it != beacon_container.upper_bound(replacement_key); ++it) {
            if (it->second == the_beacon) {
                beacon_container.erase(it--);
                break;
            }
        }
    }

    ld OnDemandOptimization::calculate_score(const optimization_target_t *optimization_target,
                                             const static_info_extension_t &static_info_extension) {
        ld score = 0;
        ld weights_sum = 0;
        for (auto const &criteria : optimization_target->criteria) {
            weights_sum += criteria.second;
            if (criteria.first == static_info_type_t::LATENCY) { // in ms, assuming max latency is 1000 ms
                score += (1 - static_info_extension.at(static_info_type_t::LATENCY) / 1000.0) * criteria.second;
            } else if (criteria.first == static_info_type_t::BW) { // in Gbps, assuming max BW is 400 Gbps
                score += static_info_extension.at(static_info_type_t::BW) / 400.0 * criteria.second;
            } else if (criteria.first == static_info_type_t::CO2) { // in g/Gbps, assuming max is 10 g/Gbps
                score += (1 - static_info_extension.at(static_info_type_t::CO2) / 10.0) * criteria.second;
            }
        }
        return (score / weights_sum);
    }

    void OnDemandOptimization::update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) {}

} // namespace ns3