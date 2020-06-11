//
// Created by chrissy on 10.06.20.
//

#include "beaconing_strategy.h"

void BeaconingStrategy::AdjustBeaconValidity (beacon* the_beacon, SCION_Node* node) {
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);
    uint16_t src_as = *the_beacon->the_path->at(0);
    if (the_beacon->is_new) {
        the_beacon->is_new = false;

        if (the_beacon->next_expiration_time > node->now) {

            if (!the_beacon->is_valid) {
                if (node->valid_beacons_count_per_src_as.find(src_as) != node->valid_beacons_count_per_src_as.end()) {
                    node->valid_beacons_count_per_src_as.at(src_as)++;
                } else {
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
        node->valid_beacons_count_per_src_as.at(src_as) = node->valid_beacons_count_per_src_as.at(src_as) - 1;
        node->next_round_valid_beacons_count_per_src_as.at(src_as) = node->next_round_valid_beacons_count_per_src_as.at(src_as) - 1;

        if (node->valid_beacons_count_per_src_as.at(src_as) == 0) {
            node->valid_beacons_count_per_src_as.erase(src_as);
        }

        if (node->next_round_valid_beacons_count_per_src_as.at(src_as) == 0) {
            node->next_round_valid_beacons_count_per_src_as.erase(src_as);
        }
    }
}

void BeaconingStrategy::DoBeaconing(SCION_Node* node) { // TODO: Second argument for the "allowed interfaces"?
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);

    node->bytes_sent_per_interface_per_period.insert(std::make_pair(node->now, std::vector<uint32_t > (node->GetNDevices(), 0)));

    this->DisseminateBeacons();
    this->InitiateBeacons();
}
