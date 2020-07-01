/**
 * @file beaconing_strategy.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @brief Defines the base class and behaviours of a strategy for beaconing & its associated
 * constants.
 */

#ifndef SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
#define SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H

#include <unordered_map>
#include "beacon.h"
#include "scion_node.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

// TODO: Consider moving all the constants and macros into a config file?
const uint8_t MAX_IMMEDIATE_BEACONS = 5;

class BeaconingStrategy{
public:
    /**
     * @brief Iterates over all the valid interfaces of the nodes neighbours and generates and sends a new beacon on each.
     */
    void InitiateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node);

    /**
     * @brief Sends the beacon to each neighbour over the lowest latency interface if the beacon originated at an AS
     * towards which the node has not yet discovered any paths.
     */
    void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node);

    /**
    * @brief Iterates over all the beacons in the beacon store and adjusts their validity.
    */
    static void UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node* node);

    /**
     * @brief Called to initiate the dissemination of beacons.
     *
     * Must be overwritten by descendants of BeaconingStrategy.
     */
    virtual void DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node) = 0;

protected:
    /**
     * @brief Updates the nodes time with the current simulator time, updates beacon attributes and
     * the nodes valid beacon counters depending on the beacon state.
     */
    static void AdjustBeaconValidity(beacon* the_beacon, SCION_Node* node);

    /**
     * @brief Checks if the addition of the remote AS to the beacon would generate a loop in the AS-level path.
     */
    static bool generates_loop(beacon const* the_beacon, uint16_t remote_as_no);

    /**
     * @brief Fetches the remote interface number and a handle to the remote AS given an egress interface on the node.
     */
    static std::pair<uint16_t, ns3::Ptr<SCION_Node>> GetRemoteAsInfo(SCION_Node* node, uint16_t egress_interface_no);

    /**
     * @brief Creates the new beacon if necessary, updates the structures recording how many bytes were sent per interface,
     * writes the new beacon into the remote ASes beacon store structures and schedules a processing event if the beacon needs
     * to continue being disseminated right away.
     */
    void GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                               SCION_Node* node, SCION_Node* remote_as,
                                       ld latency, ld bwd, bool immediate, ld latency_for_immediate);


    // TODO: Better name?
    /**
     * @brief Defines the behaviour of the beaconing strategy when a new beacon is received but the storing limits for
     * this AS is already reached in the beacon store.
     *
     * Must be overwritten by descendants of BeaconingStrategy.
     */
    virtual void HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                       SCION_Node* node, SCION_Node* remote_as,
                                       ld latency, ld bwd) = 0;
    /**
     * @brief Handles updating of any additional beacon_store structures needed for this beaconing strategy.
     * // TODO: Should we shift those specialized data structures onto the beaconing strategy? => Yes probably. Would make sense.
     *
     * Must be overwritten by descendants of BeaconingStrategy.
     */
    virtual void UpdateSpecializedBeaconStore(SCION_Node* remote_as, ld latency, ld bwd, uint16_t src_as_no, beacon *new_beacon) = 0;

};
#endif //SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
