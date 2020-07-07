/**
 * @file scion_core_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_core_as.h
 *
 */

#include "../headers/utils.h"
#include "../headers/scion_core_as.h"
#include "../headers/beaconing_strategy.h"

/**
 * Updates the simulator time & allocates memory for the statistics of this beaconing period,
 * queries which interfaces are valid for core beaconing (only core links) and initiates beacon dissemination
 * and initiation through the beaconing strategy.
 *
 * @see UpdateTimeAndStats
 * @see DisseminateBeacons
 * @see InitiateBeacons
 */
void SCION_Core_As::CoreBeaconing(){
    UpdateTimeAndStats();
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CORE);
    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
}

/**
 * Updates the simulator time & allocates memory for the statistics of this period, queries the interfaces traversed for
 * intra ISD beaconing (only customer links) and initiates the beacons through the beaconing strategy.
 *
 * @see UpdateTimeAndStats
 * @see InitiateBeacons
 */
void SCION_Core_As::IntraISDBeaconing() {
    UpdateTimeAndStats();
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);
    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
    // Core ASes never dissiminate intra_ISD beacons
}

/**
 * Queries the valid interfaces for this kind of node and processes the received beacons through the beaconing strategy.
 * @param src_as_no (src AS of the node originating beacon) TODO: Check if this is fine in processImmediate... Nope, process Immediate expects the neighbour as no
 * @param ingress_if
 * @param the_beacon
 */
void SCION_Core_As::ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon){
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::CORE);
    this->strategy->processImmediateReceive(src_as_no, ingress_if, the_beacon, valid_interfaces, this);
}