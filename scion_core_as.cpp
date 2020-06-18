//
// Created by chrissy on 10.06.20.
//

#include "scion_core_as.h"

// TODO: This was only core beaconing in the old files. Split into core vs. non core
// As far as I understand, only the core ASes ever call InitiateBeacons
// While both the Core_Ases and the Leaf_Ases need to call Disseminate Beacons in
// The function that will be passed to the scheduler
/*void BeaconingStrategy::DoBeaconing(SCION_Node* node) { // TODO: Second argument for the "allowed interfaces"?
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);

    node->bytes_sent_per_interface_per_period.insert(std::make_pair(node->now, std::vector<uint32_t > (node->GetNDevices(), 0)));

    this->DisseminateBeacons();
    this->InitiateBeacons();
}*/

void SCION_Core_As::CoreBeaconing(){
    // TODO: Implement
}

void SCION_Core_As::IntraISDBeaconing() {
    // TODO: Implement
}

std::unordered_map<uint16_t, std::vector<uint16_t>> SCION_Core_As::select_valid_interfaces(){
    // TODO: Implement
    uint16_t dummy = 0;
    std::vector<uint16_t> dummy_v = std::vector<uint16_t>();
    return std::unordered_map<uint16_t, std::vector<uint16_t>>({{dummy, dummy_v}});
}