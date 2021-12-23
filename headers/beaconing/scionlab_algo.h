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

    class SCIONLAB : public BeaconServer
    {
    public:
        SCIONLAB (bool parallel_scheduler, beaconing_timing_params params) : BeaconServer(parallel_scheduler, params) {}

        void
        DisseminateBeacons (neighbour_relation relation) override;

        void DoInitializations(uint32_t all_nodes) override;

        void
        InsertToStrategyMetaData (Beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) override;

        void
        DeleteFromStrategyMetaData (Beacon* the_beacon) override;

    protected:
        std::tuple<bool, bool, bool, Beacon*>
        ImportPolicy (Beacon& the_beacon,
                      uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                      uint16_t now) override;

        void MetaDataUpdatePeriodic (Beacon* the_beacon, bool invalidated) override;

        std::pair<Beacon*, int32_t> SelectMostDiverse (std::vector<Beacon*>& beacons, Beacon* the_beacon);

        int32_t Diversity (Beacon* beacon1, Beacon* beacon2);

    private:
        void create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no) override;

    };
}
#endif //NS_3_BEACONING_SIMULATOR_SCIONLAB_ALGO_H
