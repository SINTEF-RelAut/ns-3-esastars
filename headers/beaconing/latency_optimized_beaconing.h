/**
 * @file latency_optimized_beaconing.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see beaconing_strategy.h
 * @brief Defines a specialized beaconing beaconServer that optimizes latency
 */
#include "src/SCION/headers/beaconing/beacon_server.h"

#ifndef NS_3_BEACONING_SIMMULATOR_LATENCY_OPTIMIZED_BEACONING_H
#define NS_3_BEACONING_SIMMULATOR_LATENCY_OPTIMIZED_BEACONING_H
namespace ns3 {

#define MAX_BEACONS_TO_SEND_PER_IFACE 1
#define MAX_BEACONS_TO_STORE_PER_IFACE 1


    class LatencyOptimized : public BeaconServer
    {
    public:
        LatencyOptimized (bool parallel_scheduler, beaconing_timing_params params) : BeaconServer(parallel_scheduler, params) {}

        std::vector<std::vector<std::multimap<ld, Beacon*> > > beacons_per_dst_per_ing_if_sorted_by_latency;

        void DoInitializations(uint32_t all_nodes) override;
        /**
         * @brief Disseminates highest scoring beacons towards multiple interfaces of the appropriate neighbours until the limit for
         * sending beacons with the same originating source AS to one neighbour is reached.
         */
        void
        DisseminateBeacons(neighbour_relation relation) override;

        /**
        * @brief Evicts the lowest scored beacon for the beacons originating AS if the score of the new beacon is larger
        * than the lowest scored matching beacon found in the remote ASes beacon store.
        */

        std::tuple<bool, bool, bool, Beacon*>
        ImportPolicy(Beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                     uint16_t self_ingress_if_no, uint16_t now) override;

        void
        InsertToStrategyMetaData (Beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) override;

        void
        DeleteFromStrategyMetaData (Beacon* the_beacon) override;

    protected:

        void
        MetaDataUpdatePeriodic (Beacon* the_beacon, bool invalidated) override;

    private:
        void insert_to_beacons_per_dst_sorted_by_latency(uint16_t dst_as, Beacon* beacon);

        void delete_from_beacons_per_dst_sorted_by_latency(uint16_t dst_as, Beacon* beacon);

        std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, SCION_AS*, ld, ld> >
        select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                      const beacons_with_same_dst_as &beacons_to_the_dst_as);
    };
}
#endif //NS_3_BEACONING_SIMMULATOR_LATENCY_OPTIMIZED_BEACONING_H
