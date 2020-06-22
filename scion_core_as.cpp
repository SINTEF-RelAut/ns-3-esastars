//
// Created by chrissy on 10.06.20.
//

#include "scion_core_as.h"
#include "beaconing_strategy.h"

void SCION_Core_As::CoreBeaconing(){
    UpdateTimeAndStats();

    // TODO: Check; Is it possible for Core-ASes to still have providers? Then we need to rethink this.
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::PEER);

    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
}

void SCION_Core_As::IntraISDBeaconing() {
    UpdateTimeAndStats();

    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);

    this->strategy->InitiateBeacons(valid_interfaces_per_as, this);
    // Core ASes never dissiminate intra_ISD beacons
}

void SCION_Core_As::ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon){
    // TODO: Check; Is it possible for Core-ASes to still have providers? Then we need to rethink this.
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::PEER);
    this->strategy->processImmediateReceive(src_as_no, ingress_if, the_beacon, valid_interfaces, this);
}