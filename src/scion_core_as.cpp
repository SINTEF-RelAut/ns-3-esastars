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
 * Updates the simulator time & allocates memory for the statistics of this period, queries the interfaces traversed
 * for core beaconing and initiates beacon dissemination and initiation through the beaconing strategy.
 *
 * @see scion_node UpdateTimeAndStats
 * @see beaconing_strategy DisseminateBeacons
 * @see beaconing_strategy InitiateBeacons
 */
void SCION_Core_As::CoreBeaconing(){
    UpdateTimeAndStats();
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CORE);
    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
}

/**
 * Updates the simulator time & allocates memory for the statistics of this period, queries the interfaces traversed for
 * intra ISD beaconing and initiates the beacons through the beaconing strategy.
 *
 * @see scion_node UpdateTimeAndStats
 * @see beaconing_strategy InitiateBeacons
 */
void SCION_Core_As::IntraISDBeaconing() {
    UpdateTimeAndStats();
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);
    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
    // Core ASes never dissiminate intra_ISD beacons
}

// TODO
void SCION_Core_As::ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon){
    // TODO: Check; Is it possible for Core-ASes to still have providers? Then we need to rethink this.
    // TODO: Rethink with "Core" type
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::CORE);
    this->strategy->processImmediateReceive(src_as_no, ingress_if, the_beacon, valid_interfaces, this);
}