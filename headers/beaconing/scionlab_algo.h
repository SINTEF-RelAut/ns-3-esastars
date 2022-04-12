/**
 * @file scionlab_algo.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2020
 */

#ifndef NS_3_BEACONING_SIMULATOR_SCIONLAB_ALGO_H
#define NS_3_BEACONING_SIMULATOR_SCIONLAB_ALGO_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {
#define MAX_SET_SIZE 100

    class SCIONLAB : public BeaconServer {
    public:
        SCIONLAB(bool parallel_scheduler, beaconing_timing_params params) : BeaconServer(parallel_scheduler, params) {}

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

        std::pair<Beacon *, int32_t> select_most_diverse(std::vector<Beacon *> &beacons, Beacon *the_beacon);

        static int32_t calc_diversity(Beacon *beacon1, Beacon *beacon2);
    };
} // namespace ns3
#endif //NS_3_BEACONING_SIMULATOR_SCIONLAB_ALGO_H
