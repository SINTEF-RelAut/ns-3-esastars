/**
 * @file green_beaconing.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 */

#ifndef NS_3_BEACONING_SIMULATOR_GREEN_BEACONING_H
#define NS_3_BEACONING_SIMULATOR_GREEN_BEACONING_H

#include <cmath>
#include <yaml-cpp/yaml.h>

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

#define MAX_BEACONS_TO_SEND_PER_IFACE 1
#define MAX_BEACONS_TO_STORE_PER_IFACE 1

    class GreenBeaconing : public BeaconServer {
    public:
        GreenBeaconing(bool parallel_scheduler, beaconing_timing_params params, float dirty_energy_ratio,
                       float sun_energy_ratio)
            : BeaconServer(parallel_scheduler, params, dirty_energy_ratio, sun_energy_ratio) {}

        void DoInitializations(uint32_t num_ASes) override;

        const std::vector<std::vector<std::multimap<ld, Beacon *>>> &GetBeaconsSortedByPollution() const;

    private:
        std::vector<std::vector<std::multimap<ld, Beacon *>>> beacons_per_dst_per_ing_if_sorted_by_pollution;

        void disseminate_beacons(neighbour_relation relation) override;

        std::tuple<bool, bool, bool, Beacon *, ld> alg_specific_import_policy(Beacon &the_beacon, uint16_t sender_as,
                                                                              uint16_t remote_egress_if_no,
                                                                              uint16_t self_ingress_if_no,
                                                                              uint16_t now) override;

        void insert_to_algorithm_data_structures(Beacon *the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                                 uint16_t self_ingress_if_no) override;

        void delete_from_algorithm_data_structures(Beacon *the_beacon, ld replacement_key) override;

        void create_initial_static_info_extension(static_info_extension_t &static_info_extension,
                                                  uint16_t self_egress_if_no,
                                                  const optimization_target_t *optimization_target) override;

        void update_algorithm_data_structures_periodic(Beacon *the_beacon, bool invalidated) override;

        void select_beacons_to_disseminate_per_dst_per_nbr(
                uint16_t remote_as_no, uint16_t dst_as_no, const beacons_with_same_dst_as &beacons_to_the_dst_as,
                std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS *, static_info_extension_t>>
                        &pollution_index_map_to_beacon_and_metadata);

        ld calculate_pollution_between_border_routers(uint16_t ingress_if, uint16_t egress_if);

        friend void ReadBr2BrEnergy(ns3::NodeContainer AS_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no,
                                    const YAML::Node &config);
    };

    void ReadBr2BrEnergy(NodeContainer AS_nodes, std::map<int32_t, uint16_t> real_to_alias_as_no,
                         const YAML::Node &config);
} // namespace ns3
#endif //NS_3_BEACONING_SIMULATOR_GREEN_BEACONING_H
