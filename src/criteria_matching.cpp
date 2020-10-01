/**
 * @file criteria_matching.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see criteria_matching.h
 * @brief Implements the specialized functions for the criteria matching strategy.
 */
#include<omp.h>
#include<assert.h>
#include "../headers/criteria_matching.h"
#include "../headers/utils.h"
#include "ns3/point-to-point-channel.h"

/**
 * Iterates over all the beacons for all the neighbours of the node. Finds the beacons with the highest scores (as many as
 * FIXED_BEACONS_NUMBER_TO_SEND, based on the latency and bandwidth stats) which do not generate loops and disseminates those
 * along the appropriate interfaces.
 *
 * @see GenerateBeaconAndSend
 * @param valid_interfaces The interfaces along which to disseminate beacons for this type of node.
 * @param node The node which is disseminating beacons.
 */
 namespace ns3 {
void
CriteriaMatching::DisseminateBeacons (
    SCION_Node::neighbour_relation relation, SCION_Node *node)
{
}


void
CriteriaMatching::ReplacementPolicy (std::string key, uint16_t src_as, beacon *old_beacon,
                                     uint16_t self_egress_if_no, uint16_t remote_ingress_if_no,
                                     Ptr<SCION_Node> node, Ptr<SCION_Node> remote_as, ld latency,
                                     ld bwd)
{
    return;
}


 void
 CriteriaMatching::MetaDataUpdateAfterSend (beacon *the_beacon, uint16_t local_iface, Ptr<SCION_Node> remote_as,
                                    uint16_t dst_as_no)
 {
     return;
 }

 void
 CriteriaMatching::MetaDataUpdatePeriodic (beacon* the_beacon)
 {
     return;
 }

}





