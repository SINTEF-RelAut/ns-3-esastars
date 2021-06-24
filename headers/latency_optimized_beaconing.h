/**
 * @file latency_optimized_beaconing.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see beaconing_strategy.h
 * @brief Defines a specialized beaconing strategy that optimizes latency
 */
#include "beaconing_strategy.h"

#ifndef NS_3_BEACONING_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
#define NS_3_BEACONING_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
namespace ns3 {
#ifndef MAX_BEACON_NUMBERS_TO_STORE
#define MAX_BEACON_NUMBERS_TO_STORE 60
#endif

#ifndef MAX_BEACONS_TO_STORE_PER_IFACE
#define MAX_BEACONS_TO_STORE_PER_IFACE 1
#endif

    class LatencyOptimized : public BeaconingStrategy
    {
    public:
        std::vector<std::vector<std::multimap<ld, beacon*> > > beacons_per_dst_per_ing_if_sorted_by_latency;

        void DoInitializations(uint32_t all_nodes) override;
        /**
         * @brief Disseminates highest scoring beacons towards multiple interfaces of the appropriate neighbours until the limit for
         * sending beacons with the same originating source AS to one neighbour is reached.
         */
        void
        DisseminateBeacons(SCION_Node::neighbour_relation relation) override;

        /**
        * @brief Evicts the lowest scored beacon for the beacons originating AS if the score of the new beacon is larger
        * than the lowest scored matching beacon found in the remote ASes beacon store.
        */

        std::tuple<bool, bool, bool, beacon*>
        ImportPolicy(beacon &the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                               uint16_t self_ingress_if_no, uint16_t now) override;

        void
        InsertToStrategyMetaData (beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no, uint16_t self_ingress_if_no) override;

        void
        DeleteFromStrategyMetaData (beacon* the_beacon) override;

    protected:
        void
        MetaDataUpdateAfterImmediateSend(beacon *the_beacon, uint16_t self_egress_if_no, Ptr<SCION_Node> remote_as,
                                         uint16_t dst_as_no) override;

        void
        MetaDataUpdatePeriodic (beacon* the_beacon, bool invalidated) override;

    private:
        void insert_to_beacons_per_dst_sorted_by_latency(uint16_t dst_as, beacon* beacon);

        void delete_from_beacons_per_dst_sorted_by_latency(uint16_t dst_as, beacon* beacon);

        std::multimap<ld, std::tuple<beacon *, uint16_t, uint16_t, Ptr<SCION_Node>, ld, ld> >
        select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                      const beacons_with_same_dst_as &beacons_to_the_dst_as);
    };
}
#endif //NS_3_BEACONING_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
