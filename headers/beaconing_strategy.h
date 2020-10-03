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
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "../headers/scion_node.h"

namespace ns3 {

/**
 * @brief Maximum number of beacons to immediately disseminate per neighbour and beacon source AS.
 */
#define MAX_IMMEDIATE_BEACONS 5
#define NUM_CORE 128
class BeaconingStrategy
{
  public:
    void SetNode(Ptr<SCION_Node> node);
    /**
     * @brief Iterates over all the valid interfaces of the nodes neighbours and generates and sends a new beacon on each.
     */
    void
    InitiateBeacons (SCION_Node::neighbour_relation relation);

    /**
     * @brief Sends the beacon to each neighbour over the lowest latency valid interface if the beacon originated at an AS
     * towards which the node has not yet discovered any paths.
     */
    void processImmediateReceive (
        uint16_t beacon_origin_as_no, uint16_t ingress_if, beacon *the_beacon,
        SCION_Node::neighbour_relation relation);

    /**
    * @brief Iterates over all the beacons in the beacon store and adjusts their validity.
    */
    void UpdateStatePeriodic ();

    // TODO: This could be unified further. Still some duplicate code.
    /**
     * @brief Called to initiate the dissemination of beacons.
     *
     * Must be overwritten by descendants of BeaconingStrategy.
     */
    virtual void
    DisseminateBeacons (SCION_Node::neighbour_relation relation) = 0;

  protected:
    Ptr<SCION_Node> node;

    /**
     * @brief Updates the node time with the current simulator time, updates beacon attributes and
     * the nodes valid beacon counters depending on the beacon state.
     */
    void UpdateBeaconState (beacon *the_beacon);

    /**
     * @brief Checks if the addition of the remote AS to the beacon would generate a loop in the AS-level path.
     */
     bool GeneratesLoop (beacon const *the_beacon, uint16_t remote_as_no);

    /**
     * @brief Fetches the remote interface number and a handle to the remote AS given an egress interface on the node.
     */
     std::pair<uint16_t, Ptr<SCION_Node>> GetRemoteAsInfo (uint16_t egress_interface_no);

    /**
     * @brief Creates the new beacon if necessary, updates the structures recording how many bytes were sent per interface,
     * writes the new beacon into the remote ASes beacon store structures if the remote ASes import policy does not discard it,
     * and schedules a processing event on the simulator if the beacon needs to continue being disseminated right away.
     */
    void GenerateBeaconAndSend (beacon *old_beacon, uint16_t self_egress_if_no,
                                uint16_t remote_ingress_if_no, Ptr<SCION_Node> remote_as, ld latency, ld bwd, bool immediate,
                                ld latency_for_immediate);

    /**
     * @brief Implements the decision logic of the remote AS in case a beacon arrives that does not fit into the beacon store anymore.
     *
     * Must be called via the remote_ases strategy handler, since this is the strategy that matters.
     *
     * Must be overwritten by descendants of BeaconingStrategy.
     */
    virtual void ReplacementPolicy (std::string key, uint16_t dst_as, beacon *old_beacon,
                                        uint16_t self_egress_if_no, uint16_t remote_ingress_if_no,
                                        Ptr<SCION_Node> remote_as, ld latency,
                                        ld bwd) = 0;


    virtual void MetaDataUpdateAfterSend (beacon *the_beacon, uint16_t local_iface, Ptr<SCION_Node> remote_as,
                                               uint16_t dst_as_no) = 0;

    virtual void MetaDataUpdatePeriodic (beacon* the_beacon) = 0;
};
}
#endif //SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
