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
    // TODO: This should probably be done at instantiation time and already saved in this form. Then we can just
    // send it directly instead of recomputing every time..
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = std::unordered_map<uint16_t, std::vector<uint16_t>>();
    for( auto [neighbour_as_no, interfaces]: this->interfaces_per_neighbor_as ){
        std::vector<uint16_t> valid_interfaces = std::vector<uint16_t>();
        for( auto [intf_no, relation]: interfaces ){
            switch(relation){
                case PEER:
                    valid_interfaces.push_back(intf_no);
                case PROVIDER:
                    valid_interfaces.push_back(intf_no);
                case CUSTOMER:
                    valid_interfaces.push_back(intf_no);
            }
        }
        if(!valid_interfaces.empty()){
            valid_interfaces_per_as.insert({neighbour_as_no, valid_interfaces});
        }
    }
    return valid_interfaces_per_as;
}