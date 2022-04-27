//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#ifndef SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
#define SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
    typedef std::multimap<ld, Beacon *, std::greater<ld>> beacons_with_the_same_opt_target_and_ingress_if_t;
    typedef std::map<uint16_t, beacons_with_the_same_opt_target_and_ingress_if_t> beacons_with_the_same_opt_target_t;

    class OnDemandOptimization : public BeaconServer {
    public:
        OnDemandOptimization(SCION_AS *AS, bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                             const YAML::Node &config)
            : BeaconServer(AS, parallel_scheduler, xml_node, config) {}

        void DoInitializations(uint32_t num_ASes, rapidxml::xml_node<> *xml_node, const YAML::Node &config) override;

    private:
        std::unordered_map<const optimization_target_t *, beacons_with_the_same_opt_target_t>
                push_based_beacons_grouped_by_optimization_targets_and_ingress_if; // permanent until beacons expiration
        std::unordered_map<const optimization_target_t *, beacons_with_the_same_opt_target_t>
                pull_based_beacons_grouped_by_optimization_targets_and_ingress_if; // gets wiped out at every beaconing interval

        std::unordered_map<uint16_t, const optimization_target_t> set_of_optimization_targets_originated_from_this_as;
        std::multimap<uint16_t, const optimization_target_t *> if_to_optimization_targets_map;

        std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::vector<uint16_t>>>
                interface_groups_connected_per_neighbor; // key1: neighbor AS, key2: interface_group, values in the vector: interface ids

        uint32_t push_based_to_pull_based_frequency_ratio;
        std::set<const optimization_target_t> pull_based_optimization_targets;
        std::unordered_map<uint16_t, std::set<uint32_t>*> set_of_forbidden_edges_per_destination_as;

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
                uint16_t remote_as_no, uint16_t dst_as_no,
                const beacons_with_the_same_opt_target_t &beacons_with_the_same_opt_target,
                const optimization_target_t *optimization_target,
                std::unordered_map<
                        uint16_t,
                        std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>,
                                      std::greater<ld>>> &selected_beacons);

        void extend_static_info_extension(const Beacon *the_beacon, uint16_t beacon_ingress_if_no,
                                          uint16_t candidate_egress_if_no,
                                          static_info_extension_t &propagation_static_info);

        void update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) override;
    };
} // namespace ns3
#endif //SCION_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
