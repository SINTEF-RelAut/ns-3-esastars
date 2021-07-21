/**
 * @file beaconing_strategy.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines the base class and behaviours of a beaconServer for beaconing & its associated
 * constants.
 */

#ifndef SCION_BEACONING_SIMMULATOR_BEACON_SERVER_H
#define SCION_BEACONING_SIMMULATOR_BEACON_SERVER_H

#include <unordered_set>
#include <unordered_map>
#include <map>

#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/local_scheduler.h"

namespace ns3 {
#define NUM_CORE 128
#define MAX_BEACONS_TO_STORE 10000
#define MAX_BEACONS_TO_SEND 20
    class SCION_AS;

/** @brief Holds a set of beacons with constant length.*/
    typedef std::unordered_set<Beacon *> beacons_with_equal_length;

/**
 * @brief Holds beacons originating at a fixed destination AS indexable by their hop count.
 */
    typedef std::map<uint16_t, beacons_with_equal_length> beacons_with_same_dst_as;

/**
 * @brief Holds 0:beaconing_period, 1:expiration_period.
 *
 * Used to reduce the number of parameters we need to pass into the CreateObject constructor wrapper.
 *
 * Expected order:
 * - beaconing_period
 * - expiration_period
 */
    typedef std::pair<Time, uint16_t> beaconing_timing_params;



    class BeaconServer {
    public:
        BeaconServer(beaconing_timing_params params) : beaconing_period(params.first),
                                                       expiration_period(params.second) {}

        // beacon store structures ***************************************************************************************************
        /** @brief Pointers to all the beacons indexable by their destination AS and their hop count.*/
        std::unordered_map<uint16_t, beacons_with_same_dst_as> beacon_store;
        /** @brief Pointers to all the beacons indexable by their key.
         *
         * This is done to allow efficient traversal & search of all the beacons.
         * @see key
         * */
        std::unordered_map<std::string, Beacon *> path_map_to_beacon;
        // statistics ***************************************************************************************************************
        /** @brief Holds the number of beacons that are valid for each destination AS in the current beaconing period.*/
        std::unordered_map<uint16_t, uint16_t> valid_beacons_count_per_dst_as;
        /** @brief Collects how many bytes would have been sent over which interface for every beacon sent during one beaconing_period.
         *
         * The outer map structure is indexed by time. Then the vector index corresponds to the interface number.
         * */
        std::unordered_map<uint16_t, std::vector<uint32_t>> bytes_sent_per_interface_per_period;

        void SetNode(Ptr<SCION_AS> node);

        virtual void DoInitializations(uint32_t all_nodes) = 0;

        /**
         * @brief Iterates over all the valid interfaces of the nodes neighbours and generates and sends a new beacon on each.
         */
        void
        InitiateBeacons(neighbour_relation relation);


        /**
        * @brief Iterates over all the beacons in the beacon store and adjusts their validity.
        */
        void UpdateStatePeriodic();

        /**
         * @brief Called to initiate the dissemination of beacons.
         *
         * Must be overwritten by descendants of BeaconServer.
         */
        virtual void
        DisseminateBeacons(neighbour_relation relation) = 0;


        /**
     * @brief Implements the decision logic of the remote AS in case a beacon arrives that does not fit into the beacon store anymore.
     *
     * Must be called via the remote_ases beaconServer handler, since this is the beaconServer that matters.
     *
     * Must be overwritten by descendants of BeaconServer.
     */
        virtual std::tuple<bool, bool, bool, Beacon *> ImportPolicy(Beacon &the_beacon,
                                                                    uint16_t sender_as, uint16_t remote_egress_if_no,
                                                                    uint16_t self_ingress_if_no,
                                                                    uint16_t now) = 0;

        virtual void InsertToStrategyMetaData(Beacon *the_beacon, uint16_t sender_as, uint16_t remote_egress_if_no,
                                              uint16_t self_ingress_if_no) = 0;

        virtual void DeleteFromStrategyMetaData(Beacon *the_beacon) = 0;

        virtual void InsertBeacon(Beacon &the_beacon, uint16_t dst_as, uint16_t sender_as, uint16_t remote_egress_if,
                                  uint16_t local_ingress_if, bool path_exists, bool existing_path_valid,
                                  Beacon *beacon_to_replace);

        virtual void DeleteBeacon(Beacon *to_be_removed_beacon, uint16_t dst_as);

        void
        IncrementControlPlaneBytesSent(Beacon &the_beacon, uint16_t interface);

        void
        ReceiveBeacon(Beacon &received_beacon, uint16_t sender_as, uint16_t remote_if, uint16_t local_if);

        void
        UpdateTimeAndStats();

        const uint16_t
        GetCurrentTime() const;

        void ScheduleBeaconing(Time last_beaconing_event_time);
    protected:
        Ptr<SCION_AS> node;

        /** @brief The current simulator time in minutes. */
        uint16_t now;
        /** @brief The next beaconing interval in minutes. */
        uint16_t next_period;
        /** @brief Periodicity of beaconing. */
        Time beaconing_period;
        /** @brief Expiration time of beacon. */
        uint16_t expiration_period;


        // helper structures ********************************************************************************************************
        /**
         * @brief This structure counts how many valid beacons will be known per destination AS after the current beaconing
         * period is complete.
         *
         * It is the sum of the currently valid beacons and the beacons that will be valid in the next beaconing period. This structure
         * is used to decide if an AS would discard the a newly sent beacon or not.
         *
         * @see GenerateBeaconAndSend
         */
        std::unordered_map<uint16_t, uint16_t> next_round_valid_beacons_count_per_dst_as;


        /**
         * @brief Updates the node time with the current simulator time, updates beacon attributes and
         * the nodes valid beacon counters depending on the beacon state.
         */
        void UpdateBeaconState(Beacon *the_beacon);

        /**
         * @brief Creates the new beacon if necessary, updates the structures recording how many bytes were sent per interface,
         * writes the new beacon into the remote ASes beacon store structures if the remote ASes import policy does not discard it,
         * and schedules a processing event on the simulator if the beacon needs to continue being disseminated right away.
         */
        void GenerateBeaconAndSend(Beacon *selected_beacon, uint16_t self_egress_if_no,
                                   uint16_t remote_ingress_if_no, Ptr<SCION_AS> remote_as, ld latency, ld bwd);

        void RegisterToLocalPathServer();




        virtual void MetaDataUpdatePeriodic(Beacon *the_beacon, bool invalidated) = 0;
    };
}
#endif //SCION_BEACONING_SIMMULATOR_BEACON_SERVER_H
