//
// Created by chrissy on 10.06.20.
//

#include "beaconing_strategy.h"

void BeaconingStrategy::AdjustBeaconValidity (SCION_Node* node) {
        node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);
        uint16_t src_as = node->as_number;

        if (src_as == 0) { // TODO: Move one lvl up to avoid race condition
            std::cout << "################################## " << node->now << " #########################################" << std::endl;
        }

        for (auto const &the_beacon_pair:node->path_map_to_beacon) {
            beacon* the_beacon = the_beacon_pair.second;
            uint16_t src_as = the_beacon->the_path->at(0)[0];
            if (the_beacon->is_new) {
                the_beacon->is_new = false;

                if (the_beacon->next_expiration_time > node->now) {
                    if (!the_beacon->is_valid) { // TODO: Change to if, faster than throwing exceptions
                        try {
                            node->valid_beacons_count_per_src_as.at(src_as)++;
                        } catch (std::out_of_range){
                            node->valid_beacons_count_per_src_as.insert(std::make_pair(src_as, 1));
                        }
                    }

                    the_beacon->is_valid = true;
                    the_beacon->initiation_time = the_beacon->next_initiation_time;
                    the_beacon->expiration_time = the_beacon->next_expiration_time;
                }
            }

            if (the_beacon->expiration_time <= node->now && the_beacon->is_valid) {
                the_beacon->is_valid = false;
                node->valid_beacons_count_per_src_as.at(src_as)--;
                node->next_round_valid_beacons_count_per_src_as.at(src_as)--;
            }
        }

        if (node->valid_beacons_count_per_src_as.at(src_as) == 0) {
             node->valid_beacons_count_per_src_as.erase(src_as);
        }

        if (node->next_round_valid_beacons_count_per_src_as.at(src_as) == 0) {
             node->next_round_valid_beacons_count_per_src_as.erase(src_as);
         }

        std::cout << node->as_number << "\t" <<node->valid_beacons_count_per_src_as.size() << std::endl; // Print number of source ASes

}

// TODO: Is this needed here? Or will this functionality be in the Core & Leaf AS?
void BeaconingStrategy::DoBeaconing(SCION_Node* node) { // TODO: Second argument for the "allowed interfaces"?
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);

    node->bytes_sent_per_interface_per_period.insert(std::make_pair(node->now, std::vector<uint32_t > (node->GetNDevices(), 0)));

    this->DisseminateBeacons();
    this->InitiateBeacons();
}

bool BeaconingStrategy::generates_loop(beacon const* the_beacon, uint16_t remote_as_no){
    for (auto const &link_info : *the_beacon->the_path) { // remove loops
        if (link_info[0] == remote_as_no) {
            return true;
        }
    }
    return false;
}