/**
 * @file scion_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 *
 * @brief Implements the specialized functions on the scion leaf ASes.
 */

#include "../headers/scion_as.h"
#include "../headers/beaconing_strategy.h"

namespace ns3 {

void
SCION_As::CoreBeaconing ()
{
    // Leaf ASes do not do any core beaconing
}

/**
 * Updates the simulator time & allocates memory for the statistics of this beaconing period,
 * fetches the interfaces traversed for intra ISD beaconing (only customer links)
 * and dissiminates the beacons through the beaconing strategy.
 *
 * @see UpdateTimeAndStats
 * @see DissiminateBeacons
 */
void
SCION_As::IntraISDBeaconing ()
{
    UpdateTimeAndStats ();
    // Select the valid interfaces
    this->strategy->DisseminateBeacons (neighbour_relation::CUSTOMER, this);
    // A leaf AS never initiates beacons
}

/**
 * Fetches the valid interfaces for dissemination (only customer links) and processes the
 * immediate beacon.
 *
 * @see processImmediateReceive
 * @param beacon_origin_as_no The AS number of the AS which originated the beacon.
 * @param ingress_if The ingress interface number on which the beacon was received.
 * @param the_beacon The immediate beacon.
 */
void
SCION_As::ProcessReceivedBeacons (uint16_t beacon_origin_as_no, uint16_t ingress_if,
                                  beacon *the_beacon)
{
    this->strategy->processImmediateReceive (beacon_origin_as_no, ingress_if, the_beacon,
                                             neighbour_relation::CUSTOMER, this);
}
}