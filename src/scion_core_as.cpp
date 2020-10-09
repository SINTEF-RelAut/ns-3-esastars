/**
 * @file scion_core_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_core_as.h
 *
 * @brief Implements the specialized functions on the scion core ASes.
 */

#include "../headers/utils.h"
#include "../headers/scion_core_as.h"
#include "../headers/beaconing_strategy.h"

namespace ns3 {

/**
 * Updates the simulator time & allocates memory for the statistics of this beaconing period,
 * queries which interfaces are valid for core beaconing (only core links) and initiates beacon dissemination
 * and initiation through the beaconing strategy.
 *
 * @see UpdateTimeAndStats
 * @see DisseminateBeacons
 * @see InitiateBeacons
 */
void
SCION_Core_As::CoreBeaconing ()
{
    UpdateTimeAndStats ();
    strategy->DisseminateBeacons (neighbour_relation::CORE);
    strategy->InitiateBeacons (neighbour_relation::CORE);
}

/**
 * Updates the simulator time & allocates memory for the statistics of this period, queries the interfaces traversed for
 * intra ISD beaconing (only customer links) and initiates the beacons through the beaconing strategy.
 *
 * @see UpdateTimeAndStats
 * @see InitiateBeacons
 */
void
SCION_Core_As::IntraISDBeaconing ()
{
    UpdateTimeAndStats ();
    strategy->InitiateBeacons (neighbour_relation::CUSTOMER);
    // Core ASes never dissiminate intra_ISD beacons
}

/**
 * Fetches the valid interfaces for this kind of node (only core links) and processes the received beacons through the beaconing strategy.
 * @param beacon_origin_as_no The AS number of the AS which originated the beacon.
 * @param ingress_if The ingress interface over which the beacon was received.
 * @param the_beacon The immediate beacon
 */
void
SCION_Core_As::ProcessReceivedBeacons (uint16_t beacon_origin_as_no, uint16_t ingress_if,
                                       beacon *the_beacon)
{
    strategy->processImmediateReceive (beacon_origin_as_no, ingress_if, the_beacon,
                                             neighbour_relation::CORE);
}
}