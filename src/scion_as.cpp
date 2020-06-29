/**
 * @file scion_as.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see scion_as.h
 *
 */

#include "../headers/scion_as.h"
#include "../headers/beaconing_strategy.h"

void SCION_As::IntraISDBeaconing() {
    UpdateTimeAndStats();

    // Select the valid interfaces
    // TODO: Rethink with "Core" type
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);

    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    // A leaf AS never initiates beacons
}

void SCION_As::ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon){
    // TODO: Rethink with "Core" type
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::CUSTOMER);
    this->strategy->processImmediateReceive(src_as_no, ingress_if, the_beacon, valid_interfaces, this);
}

void SCION_As::CoreBeaconing(){
    // Leaf ASes do not do any core beaconing
}
