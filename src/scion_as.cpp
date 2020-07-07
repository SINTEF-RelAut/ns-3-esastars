/**
 * @file scion_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 *
 */

#include "../headers/scion_as.h"
#include "../headers/beaconing_strategy.h"

/**
 * Does nothing. Leaf Ases do not participate in core-beaconing.
 */
void SCION_As::CoreBeaconing(){
    // Leaf ASes do not do any core beaconing
}

/**
 * Updates the simulator time & allocates memory for the statistics of this period, queries the interfaces traversed for
 * intra ISD beaconing (only customer links) and dissiminates the beacons through the beaconing strategy.
 *
 * @see UpdateTimeAndStats
 * @see DissiminateBeacons
 */
void SCION_As::IntraISDBeaconing() {
    UpdateTimeAndStats();
    // Select the valid interfaces
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);
    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    // A leaf AS never initiates beacons
}

// TODO
void SCION_As::ProcessReceivedBeacons(uint16_t beacon_origin_as_no, uint16_t ingress_if, beacon* the_beacon){
    // TODO: Rethink with "Core" type
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::CUSTOMER);
    //processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node)
    this->strategy->processImmediateReceive(beacon_origin_as_no, ingress_if, the_beacon, valid_interfaces, this);
}
