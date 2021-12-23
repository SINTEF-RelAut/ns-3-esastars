/**
 * @file beacon_server.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_BEACONING_SIMULATOR_BEACON_SERVER_H
#define SCION_BEACONING_SIMULATOR_BEACON_SERVER_H

#include <unordered_set>
#include <unordered_map>
#include <map>

#include "ns3/nstime.h"


#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/externs.h"

namespace ns3 {

#define MAX_BEACONS_TO_STORE 60
#define MAX_BEACONS_TO_SEND 5
    class SCION_AS;

    typedef std::unordered_set<Beacon *> beacons_with_equal_length;

    typedef std::map<uint16_t, beacons_with_equal_length> beacons_with_same_dst_as;

    typedef std::pair<Time, uint16_t> beaconing_timing_params;

    class BeaconServer {
    public:
        BeaconServer(bool parallel_scheduler, beaconing_timing_params params) :
                     parallel_scheduler(parallel_scheduler), beaconing_period(params.first), expiration_period(params.second)
        {}

        std::unordered_map<uint16_t, beacons_with_same_dst_as> beacon_store;

        std::unordered_map<std::string, Beacon*> path_map_to_beacon;

        std::unordered_map<uint16_t, uint16_t> valid_beacons_count_per_dst_as;

        std::unordered_map<uint16_t, std::vector<uint32_t>> bytes_sent_per_interface_per_period;

        void SetNode(SCION_AS* node);

        virtual void DoInitializations(uint32_t all_nodes) = 0;

        void
        InitiateBeacons(neighbour_relation relation);

        void UpdateStatePeriodic();

        virtual void
        DisseminateBeacons(neighbour_relation relation) = 0;

        virtual std::tuple<bool, bool, bool, Beacon*> ImportPolicy(Beacon& the_beacon,
                                                                    uint16_t sender_as, uint16_t remote_egress_if_no,
                                                                    uint16_t self_ingress_if_no,
                                                                    uint16_t now) = 0;

        virtual void InsertToStrategyMetaData(Beacon* the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                              uint16_t self_ingress_if_no) = 0;

        virtual void DeleteFromStrategyMetaData(Beacon* the_beacon) = 0;

        virtual void InsertBeacon(Beacon& the_beacon, uint16_t dst_as, uint16_t sender_as, uint16_t remote_egress_if,
                                  uint16_t local_ingress_if, bool path_exists, bool existing_path_valid,
                                  Beacon* beacon_to_replace);

        virtual void DeleteBeacon(Beacon* to_be_removed_beacon, uint16_t dst_as);

        void
        IncrementControlPlaneBytesSent(Beacon& the_beacon, uint16_t interface);

        void
        ReceiveBeacon(Beacon& received_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if);

        void
        UpdateTimeAndStats();

        const uint16_t
        GetCurrentTime() const;

        void ScheduleBeaconing(Time last_beaconing_event_time);

    protected:
        bool parallel_scheduler;
        SCION_AS* node;
        uint16_t now;
        uint16_t next_period;
        Time beaconing_period;
        uint16_t expiration_period;

        std::unordered_map<uint16_t, uint16_t> next_round_valid_beacons_count_per_dst_as;

        void UpdateBeaconState(Beacon* the_beacon);

        void GenerateBeaconAndSend(Beacon* selected_beacon, uint16_t self_egress_if_no,
                                   uint16_t remote_ingress_if_no, SCION_AS* remote_as, static_info_extension_t static_info_extension);
        void RegisterToLocalPathServer();

        virtual void MetaDataUpdatePeriodic(Beacon* the_beacon, bool invalidated) = 0;

        virtual void create_initial_static_info_extension(static_info_extension_t& static_info_extension, uint16_t self_egress_if_no);
    };
}
#endif //SCION_BEACONING_SIMULATOR_BEACON_SERVER_H
