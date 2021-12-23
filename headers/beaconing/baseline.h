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

        void
        DisseminateBeacons(neighbour_relation relation) override;

        void DoInitializations(uint32_t all_nodes) override;

        void
        InsertToStrategyMetaData(Beacon *the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                 uint16_t self_ingress_if_no) override;

        void
        DeleteFromStrategyMetaData(Beacon *the_beacon) override;

    protected:
        std::tuple<bool, bool, bool, Beacon *>
        ImportPolicy(Beacon &the_beacon,
                     uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                     uint16_t now) override;

        void MetaDataUpdatePeriodic(Beacon *the_beacon, bool invalidated) override;

    private:
        void create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no) override;
    };
}
#endif //SCION_BEACONING_SIMULATOR_BASELINE_H
