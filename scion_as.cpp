//
// Created by chrissy on 10.06.20.
//

#include "scion_as.h"

void SCION_As::IntraISDBeaconing() {
    // TODO: Implement
}

std::unordered_map<uint16_t, std::vector<uint16_t>> SCION_As::select_valid_interfaces(){
    // TODO: This should probably be done at instantiation time and already saved in this form. Then we can just
    // send it directly instead of recomputing every time..
    std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces_per_as = std::unordered_map<uint16_t, std::vector<uint16_t>>();
    for( auto [neighbour_as_no, interfaces]: this->interfaces_per_neighbor_as ){
        std::vector<uint16_t> valid_interfaces = std::vector<uint16_t>();
        for( auto [intf_no, relation]: interfaces ){
            switch(relation){
                case PEER:
                    ;// only for core, "PCBs typically don't traverse peering links (book page 22)"
                case PROVIDER:
                    ; // wrong direction
                case CUSTOMER:
                    valid_interfaces.push_back(intf_no);
            }
        }
        if( !valid_interfaces.empty() ){
            valid_interfaces_per_as.insert({neighbour_as_no, valid_interfaces});
        }
    }
    return valid_interfaces_per_as;
}