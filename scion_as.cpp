//
// Created by chrissy on 10.06.20.
//

#include "scion_as.h"
#include "beaconing_strategy.h"

void SCION_As::IntraISDBeaconing() {
    UpdateTimeAndStats();

    // Select the valid interfaces
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = GetValidInterfaces(neighbour_relation::CUSTOMER);

    this->strategy->DisseminateBeacons(valid_interfaces_per_as, this);
    // A leaf AS never initiates beacons
}

void SCION_As::ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon){
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces = this->GetValidInterfaces(neighbour_relation::CUSTOMER);
    this->strategy->processImmediateReceive(src_as_no, ingress_if, the_beacon, valid_interfaces, this);
}
