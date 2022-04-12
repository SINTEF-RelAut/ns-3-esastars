//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#include "src/SCION/headers/beaconing/on_demand_optimization.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {
    void OnDemandOptimization::DoInitializations(uint32_t num_ASes) {}

    void OnDemandOptimization::initiate_beacons_per_interface(uint16_t self_egress_if_no, SCION_AS *remote_as,
                                                              uint16_t remote_ingress_if_no) {
        //push_based
        for (auto it = if_to_optimization_targets_map.lower_bound(self_egress_if_no);
             it != if_to_optimization_targets_map.upper_bound(self_egress_if_no); ++it) {
            static_info_extension_t static_info_extension;
            create_initial_static_info_extension(static_info_extension, self_egress_if_no, it->second);
            generate_beacon_and_send(NULL, self_egress_if_no, remote_ingress_if_no, remote_as, static_info_extension,
                                     it->second, beacon_direction_t::PUSH_BASED);
        }

        //pull-based
        //        if (Simulator::Now().GetTimeStep() % beaconing_period.GetTimeStep() == 0 &&
        //           (Simulator::Now().GetTimeStep() / beaconing_period.GetTimeStep()) % push_based_to_pull_based_frequency_ratio == 0) {
        //            for (auto const& dst_as : beacon_store) {
        //
        //            }
        //        }
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

        std::multimap<ld, Beacon *>::const_iterator worst_beacon_score =
                beacon_container.at(the_beacon.optimization_target).at(self_ingress_if_no).begin();
        ld incoming_beacon_score =
                calculate_incoming_beacon_score(the_beacon, sender_as, remote_egress_if_no, self_ingress_if_no);
        ld lowest_previous_score = worst_beacon_score->first;
        if (lowest_previous_score < incoming_beacon_score) {
            Beacon *to_be_removed_beacon = worst_beacon_score->second;
            return std::tuple<bool, bool, bool, Beacon *, ld>(true, false, false, to_be_removed_beacon,
                                                              lowest_previous_score);
        }

        return std::tuple<bool, bool, bool, Beacon *, ld>(false, false, false, NULL, 0);
    }

    ld OnDemandOptimization::calculate_incoming_beacon_score(Beacon &the_beacon, uint16_t sender_as,
                                                             uint16_t remote_egress_if_no,
                                                             uint16_t self_ingress_if_no) {
        ld score = 0;
        ld weights_sum = 0;
        for (auto const &criteria : the_beacon.optimization_target->criteria) {
            weights_sum += criteria.second;
            if (criteria.first == static_info_type_t::LATENCY) { // in ms, assuming max latency is 1000 ms
                score += (1 - the_beacon.static_info_extension.at(static_info_type_t::LATENCY) / 1000.0) *
                         criteria.second;
            } else if (criteria.first == static_info_type_t::BW) { // in Gbps, assuming max BW is 400 Gbps
                score += the_beacon.static_info_extension.at(static_info_type_t::BW) / 400.0 * criteria.second;
            } else if (criteria.first == static_info_type_t::CO2) { // in g/Gbps, assuming max is 10 g/Gbps
                score += (1 - the_beacon.static_info_extension.at(static_info_type_t::CO2) / 10.0) * criteria.second;
            }
        }
        return (score / weights_sum);
    }

    void OnDemandOptimization::insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as,
                                                                   uint16_t remote_egress_if_no,
                                                                   uint16_t self_ingress_if_no) {}

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

    void OnDemandOptimization::disseminate_beacons(neighbour_relation relation) {}

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

} // namespace ns3