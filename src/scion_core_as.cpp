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

void SCION_Core_As::CoreBeaconing(){
    UpdateTimeAndStats();
    // TODO: Rethink with "Core" type
    // TODO: Check; Is it possible for Core-ASes to still have providers? Then we need to rethink this.
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::PEER);
    // TODO: Debugg
    //print_valid_intfs(this, valid_interfaces_per_as);
    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
}

void SCION_Core_As::IntraISDBeaconing() {
    UpdateTimeAndStats();
    // TODO: Rethink with "Core" type
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);

    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
    // Core ASes never dissiminate intra_ISD beacons
}

void SCION_Core_As::ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon){
    // TODO: Check; Is it possible for Core-ASes to still have providers? Then we need to rethink this.
    // TODO: Rethink with "Core" type
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::PEER);
    this->strategy->processImmediateReceive(src_as_no, ingress_if, the_beacon, valid_interfaces, this);
}