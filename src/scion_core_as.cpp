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
#include "src/SCION/headers/beaconing/beacon_server.h"

namespace ns3 {

/**
 * Updates the simulator time & allocates memory for the statistics of this beaconing period,
 * queries which interfaces are valid for core beaconing (only core links) and initiates beacon dissemination
 * and initiation through the beaconing beaconServer.
 *
 * @see UpdateTimeAndStats
 * @see DisseminateBeacons
 * @see InitiateBeacons
 */
void
SCION_Core_AS::CoreBeaconing ()
{
    beaconServer->UpdateTimeAndStats ();
    beaconServer->DisseminateBeacons (neighbour_relation::CORE);
    beaconServer->InitiateBeacons (neighbour_relation::CORE);
}

/**
 * Updates the simulator time & allocates memory for the statistics of this period, queries the interfaces traversed for
 * intra ISD beaconing (only customer links) and initiates the beacons through the beaconing beaconServer.
 *
 * @see UpdateTimeAndStats
 * @see InitiateBeacons
 */
void
SCION_Core_AS::IntraISDBeaconing ()
{
    beaconServer->UpdateTimeAndStats ();
    beaconServer->InitiateBeacons (neighbour_relation::CUSTOMER);
    // Core ASes never dissiminate intra_ISD beacons
}


}