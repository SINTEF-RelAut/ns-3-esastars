/**
 * @file baseline.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_BEACONING_SIMULATOR_BASELINE_H
#define SCION_BEACONING_SIMULATOR_BASELINE_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

    class Baseline : public BeaconServer {
    public:
        Baseline(bool parallel_scheduler, beaconing_timing_params params) : BeaconServer(parallel_scheduler, params) {}

        void DoInitializations(uint32_t num_ASes) override;

    private:
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
    };
} // namespace ns3
#endif //SCION_BEACONING_SIMULATOR_BASELINE_H
