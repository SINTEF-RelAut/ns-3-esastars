/**
 * @file baseline.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Defines specialized functions for the baseline beaconing beaconServer.
 */

#ifndef SCION_BEACONING_SIMMULATOR_BASELINE_H
#define SCION_BEACONING_SIMMULATOR_BASELINE_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

    class Baseline : public BeaconServer {
    public:
        Baseline(beaconing_timing_params params) : BeaconServer(params) {}

        /**
         * @brief Disseminates the valid beacons towards multiple interfaces of the appropriate neighbours until the limit for
         * sending beacons with equal source ASes to one neighbour is reached.
         */
        void
        DisseminateBeacons(neighbour_relation relation) override;

        void DoInitializations(uint32_t all_nodes) override;

        void
        InsertToStrategyMetaData(beacon *the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                 uint16_t self_ingress_if_no) override;

        void
        DeleteFromStrategyMetaData(beacon *the_beacon) override;

    protected:
        /**
         * @brief Does nothing. This beaconServer does not evict any beacons.
         */
        std::tuple<bool, bool, bool, beacon *>
        ImportPolicy(beacon &the_beacon,
                     uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no,
                     uint16_t now) override;

        void MetaDataUpdatePeriodic(beacon *the_beacon, bool invalidated) override;
    };
}
#endif //SCION_BEACONING_SIMMULATOR_BASELINE_H
