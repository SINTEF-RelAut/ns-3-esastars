/**
 * @file latency_optimized_beaconing.h
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 */

#ifndef NS_3_BEACONING_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
#define NS_3_BEACONING_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H

#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

#define MAX_BEACONS_TO_SEND_PER_IFACE 1
#define MAX_BEACONS_TO_STORE_PER_IFACE 1


    class LatencyOptimized : public BeaconServer
    {
    public:
        LatencyOptimized (bool parallel_scheduler, beaconing_timing_params params) : BeaconServer(parallel_scheduler, params) {}

        std::vector<std::vector<std::multimap<ld, Beacon*> > > beacons_per_dst_per_ing_if_sorted_by_latency;

        void DoInitializations(uint32_t all_nodes) override;

        void
        DisseminateBeacons(neighbour_relation relation) override;

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

        std::multimap<ld, std::tuple<Beacon*, uint16_t, uint16_t, SCION_AS*, static_info_extension_t> >
        select_beacons_to_disseminate_per_dst_per_nbr(uint16_t remote_as_no, uint16_t dst_as_no,
                                                      const beacons_with_same_dst_as& beacons_to_the_dst_as);

        void create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no) override;
    };
}
#endif //NS_3_BEACONING_SIMULATOR_LATENCY_OPTIMIZED_BEACONING_H
