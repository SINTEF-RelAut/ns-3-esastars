//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#ifndef SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
#define SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
    typedef std::multimap<ld, Beacon *, std::greater<ld>> beacons_with_the_same_opt_target_and_ingress_if_group_t;
    typedef std::map<uint16_t, beacons_with_the_same_opt_target_and_ingress_if_group_t> beacons_with_the_same_opt_target_t;
    typedef std::unordered_map<const optimization_target_t *, beacons_with_the_same_opt_target_t>
            beacons_grouped_by_optimization_targets_and_ingress_if_group_t;

    class OnDemandOptimization : public BeaconServer {
    public:
        OnDemandOptimization(SCION_AS *AS, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                             const YAML::Node &config)
            : BeaconServer(AS, parallel_scheduler, xml_node, config) {
            pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group.resize(2);
            last_push_based_interval = Time(config["beacon_service"]["last_push_based_interval"].as<std::string>()).ToInteger(Time::MIN);
            pull_based_dissemination_to_initiation_frequency = stoi(config["beacon_service"]["pull_based_dissemination_to_initiation_frequency"].as<std::string>());
            rapidxml::xml_node<> *cur_target = xml_node->first_node("target");
            while (cur_target) {
                uint16_t target_id = std::stoi(cur_target->first_node("target_id")->value());

                optimization_criteria_t optimization_criteria;
                PropertyContainer p = parseProperties(cur_target);
                if (p.hasProperty("bw") && std::stof(p.getProperty("bw")) > 0.001) {
                    optimization_criteria.insert(
                            std::make_pair(static_info_type_t::BW, std::stof(p.getProperty("bw"))));
                }

                if (p.hasProperty("latency") && std::stof(p.getProperty("latency")) > 0.001) {
                    optimization_criteria.insert(
                            std::make_pair(static_info_type_t::LATENCY, std::stof(p.getProperty("latency"))));
                }

                std::string direction = cur_target->first_node("direction")->value();
                optimization_direction_t optimization_direction = optimization_direction_t::SYMMETRIC;
                if (direction == "forward") {
                    optimization_direction = optimization_direction_t::FORWARD;
                } else if (direction == "backward") {
                    optimization_direction = optimization_direction_t::BACKWARD;
                }

                uint16_t group_id = std::stoi(cur_target->first_node("group_id")->value());
                uint16_t no_beacons = std::stoi(cur_target->first_node("no_beacons")->value());

                set_of_optimization_targets_originated_from_this_as.insert(
                        std::make_pair(target_id, optimization_target_t(target_id, optimization_criteria, optimization_direction,
                                                                        AS->as_number, group_id, no_beacons, NULL)));
                cur_target = cur_target->next_sibling("target");
            }
        }

        void DoInitializations(uint32_t num_ASes, rapidxml::xml_node<> *xml_node, const YAML::Node &config) override;

        void PerLinkInitializations(rapidxml::xml_node<> *xml_node, const YAML::Node &config) override;

    private:
        beacons_grouped_by_optimization_targets_and_ingress_if_group_t
                push_based_beacons_grouped_by_optimization_targets_and_ingress_if_group; // permanent until beacons expiration

        std::vector<beacons_grouped_by_optimization_targets_and_ingress_if_group_t> pull_based_beacons_grouped_by_optimization_targets_and_ingress_if_group;

        std::unordered_map<uint16_t, const optimization_target_t> set_of_optimization_targets_originated_from_this_as;
        std::multimap<uint16_t, const optimization_target_t *> if_to_push_based_optimization_targets_map;

        std::unordered_map<uint16_t, uint16_t> if_to_if_group;

        std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::vector<uint16_t>>>
                interface_groups_connected_per_neighbor; // key1: neighbor AS, key2: interface_group, values in the vector: interface ids

        uint16_t last_push_based_interval;
        uint16_t pull_based_dissemination_to_initiation_frequency;
        std::set<std::pair<uint16_t, uint16_t>> visited_pull_based_src_dst_pair;

        std::multimap<uint16_t, const optimization_target_t *> if_to_pull_based_optimization_targets_map;
        std::unordered_map<uint16_t, std::unordered_map<uint32_t, uint16_t> *> repetition_of_edges;
        std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::unordered_set<uint16_t>*> *> set_of_forbidden_edges_per_destination_as;
        std::unordered_set<Beacon*> new_requested_pull_based_beacons;

        void initiate_beacons_per_interface(uint16_t self_egress_if_no, SCION_AS *remote_as,
                                            uint16_t remote_ingress_if_no) override;

        void create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                  uint16_t self_egress_if_no,
                                                  const optimization_target_t *optimization_target) override;

        void disseminate_beacons(neighbour_relation relation) override;

        std::tuple<bool, bool, bool, Beacon *, ld> alg_specific_import_policy(Beacon &the_beacon, uint16_t sender_as,
                                                                              uint16_t remote_egress_if_no,
                                                                              uint16_t self_ingress_if_no,
                                                                              uint16_t now) override;

        void insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                                 uint16_t self_ingress_if_no) override;

        void delete_from_algorithm_data_structures(Beacon *the_beacon, ld replacement_key) override;

        static ld calculate_score(const optimization_target_t *optimization_target,
                                  const static_info_extension_t &static_info_extension);

        void select_beacons_to_disseminate_per_target_per_nbr(
                uint16_t remote_as_no, const beacons_with_the_same_opt_target_t &beacons_with_the_same_opt_target,
                const optimization_target_t *optimization_target,
                std::unordered_map<
                        uint16_t,
                        std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>,
                                      std::greater<ld>>> &selected_beacons);

        void send_selected_beacons_per_target_per_nbr(
                const std::unordered_map<
                        uint16_t,
                        std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>,
                                      std::greater<ld>>> &selected_beacons);

        void extend_static_info_extension(const Beacon *the_beacon, uint16_t beacon_ingress_if_no,
                                          uint16_t candidate_egress_if_no,
                                          static_info_extension_t &propagation_static_info);

        void update_state_before_beaconing() override;

        void update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) override;

        void delete_from_forbidden_edges(Beacon *the_beacon);

        void insert_to_forbidden_edges(Beacon *the_beacon);

        void create_optimization_targets_for_forbidden_edges(uint16_t dst_as);
    };
} // namespace ns3
#endif //SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
