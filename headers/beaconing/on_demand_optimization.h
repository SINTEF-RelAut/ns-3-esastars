//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#ifndef NS_3_BEACONING_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
#define NS_3_BEACONING_SIMULATOR_ON_DEMAND_OPTIMIZATION_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
    class OnDemandOptimization : public BeaconServer {
    public:
        OnDemandOptimization(bool parallel_scheduler, beaconing_timing_params params)
            : BeaconServer(parallel_scheduler, params) {}

        void DoInitializations(uint32_t num_ASes) override;

    private:
        std::unordered_map<const optimization_target_t *, std::map<uint16_t, std::multimap<ld, Beacon *>>>
                push_based_beacons_grouped_by_optimization_targets_and_ingress_if; // permanent until beacons expiration
        std::unordered_map<const optimization_target_t *, std::map<uint16_t, std::multimap<ld, Beacon *>>>
                pull_based_beacons_grouped_by_optimization_targets_and_ingress_if; // gets wiped out at every beaconing interval

        std::unordered_map<uint16_t, const optimization_target_t> set_of_optimization_targets_originated_from_this_as;
        std::multimap<uint16_t, const optimization_target_t *> if_to_optimization_targets_map;

        uint32_t push_based_to_pull_based_frequency_ratio;
        std::set<const optimization_target_t> pull_based_optimization_targets;

        void disseminate_beacons(neighbour_relation relation) override;

        std::tuple<bool, bool, bool, Beacon *, ld> alg_specific_import_policy(Beacon &the_beacon, uint16_t sender_as,
                                                                              uint16_t remote_egress_if_no,
                                                                              uint16_t self_ingress_if_no,
                                                                              uint16_t now) override;

        void insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                                 uint16_t self_ingress_if_no) override;

        void delete_from_algorithm_data_structures(Beacon *the_beacon, ld replacement_key) override;

        void initiate_beacons_per_interface(uint16_t self_egress_if_no, SCION_AS *remote_as,
                                            uint16_t remote_ingress_if_no) override;

        void create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                  uint16_t self_egress_if_no,
                                                  const optimization_target_t *optimization_target) override;

        void update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) override;

        static ld calculate_incoming_beacon_score(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                                  uint16_t self_ingress_if_no);


    };
} // namespace ns3
#endif //NS_3_BEACONING_SIMULATOR_ON_DEMAND_OPTIMIZATION_H
