/**
 * @file criteria_matching.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see criteria_matching.h
 */
#include <omp.h>
#include "../headers/criteria_matching.h"
#include "ns3/point-to-point-channel.h"

void CriteriaMatching::DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node){
#pragma omp parallel for
    for (uint32_t i = 0; i < node->neighbors.size(); ++i){
        uint16_t remote_as_no = node->neighbors.at(i);
        std::vector<uint16_t> interfaces = valid_interfaces.at(remote_as_no);
        // TODO: Figure out why it is complaining about structured bindings and change back..
        for (auto const it: node->beacon_store) { // Per source AS
            auto src_as_no = it.first;
            auto equal_src_as_beacons = it.second;
            if (remote_as_no == src_as_no) {
                continue;
            }
            std::multimap<int64_t, std::tuple<beacon*, uint16_t, uint16_t, SCION_Node*, ld , ld> > beacons_ifaces_matchings_scores;
            int16_t  sent_count = 0;
            for (auto const &len_beacons_pair : *equal_src_as_beacons) { // for each length
                if (sent_count >= FIXED_BEACONS_NUMBER_TO_SEND) {
                    break;
                }
                for (auto const &the_beacon : *len_beacons_pair.second) {
                    if (sent_count >= FIXED_BEACONS_NUMBER_TO_SEND){
                        break;
                    }
                    if (!the_beacon->is_valid || generates_loop(the_beacon, remote_as_no)) {
                        continue;
                    }
                    sent_count++;

                    // Iterate over all the valid interfaces of this remote AS and send the beacons
                    for (auto egress_interface_no: interfaces){

                        auto [remote_ingress_if_no, remote_as_ptr] = GetRemoteAsInfo(node, egress_interface_no);

                        SCION_Node* remote_as = ns3::GetPointer(remote_as_ptr);
                        ld latency = the_beacon->latency_stat + node->intra_as_latencies.at(the_beacon->the_path->back()[3]).at(egress_interface_no);
                        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(egress_interface_no)
                                 ? (ld) node->inter_as_bwds.at(egress_interface_no)
                                 : the_beacon->bwd_stat;

                        ld score = ((1 - latency / 1000) * remote_as->latency_coef +
                                    (bwd / 400) * remote_as->bandwidth_coef)
                                   / (remote_as->latency_coef + remote_as->bandwidth_coef);

                        if (beacons_ifaces_matchings_scores.size() >= FIXED_BEACONS_NUMBER_TO_SEND
                            && score <= beacons_ifaces_matchings_scores.begin()->first) {
                            continue;
                        }

                        beacons_ifaces_matchings_scores.insert(std::make_pair(score, std::tuple<beacon *, uint16_t, uint16_t, SCION_Node*, ld,
                                                                              ld>(the_beacon, egress_interface_no, remote_ingress_if_no, remote_as, latency, bwd)));

                        if (beacons_ifaces_matchings_scores.size() > FIXED_BEACONS_NUMBER_TO_SEND) {
                            beacons_ifaces_matchings_scores.erase(beacons_ifaces_matchings_scores.begin());
                        }
                        // remote_as is out of scope
                        remote_as_ptr->Unref();
                    }
                }
            }

            for (auto const &the_tuple_pair : beacons_ifaces_matchings_scores) {
                beacon *the_beacon;
                uint16_t remote_ingress_if_no;
                uint16_t egress_interface_no;
                SCION_Node* remote_as;
                ld latency;
                ld bwd;

                std::tie(the_beacon, egress_interface_no, remote_ingress_if_no, remote_as, latency, bwd) = the_tuple_pair.second;

                GenerateBeaconAndSend(the_beacon, egress_interface_no, remote_as_no, remote_ingress_if_no, node,
                                      remote_as, latency, bwd, false, 0.0);

            }

        }
    }
}

ld CriteriaMatching::CalculateBeaconScore(SCION_Node* remote_as, ld latency, ld bwd){
    return ((1 - latency / 1000) * remote_as->latency_coef + (bwd / 400) * remote_as->bandwidth_coef)
           / (remote_as->latency_coef + remote_as->bandwidth_coef);
}

void CriteriaMatching::HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                             SCION_Node* node, SCION_Node* remote_as,
                                             ld latency, ld bwd){
    // Returns true if the beacon store was full
    ld score = CalculateBeaconScore(remote_as, latency, bwd);
    // Check against the lowest score beacons if we need to replace one
    std::multimap <ld, beacon* >::iterator it = remote_as->beacons_sorted_by_score.at(src_as)->begin();
    if (it->first < score) {
        beacon* lower_score_beacon = it->second;
        uint16_t path_len = lower_score_beacon->the_path->size();
        remote_as->beacons_sorted_by_score.at(src_as)->erase(it);
        remote_as->path_map_to_beacon.erase(lower_score_beacon->key);
        remote_as->beacon_store.at(src_as)->at(path_len)->erase(lower_score_beacon);
        if (remote_as->beacon_store.at(src_as)->at(path_len)->empty()) {
            remote_as->beacon_store.at(src_as)->erase(path_len);
        }

        if (lower_score_beacon->is_valid) {
            remote_as->valid_beacons_count_per_src_as.at(src_as)--;
        }

        *lower_score_beacon->the_path = *old_beacon->the_path; // TODO: Need to add case where beacon is null

        uint16_t *link_info = new uint16_t[4]; // TODO: Use typedef
        link_info[0] = node->as_number;
        link_info[1] = self_egress_if_no;
        link_info[2] = remote_as_no;
        link_info[3] = remote_ingress_if_no;

        lower_score_beacon->the_path->push_back(link_info);
        lower_score_beacon->key = key;
        lower_score_beacon->initiation_time = -1;
        lower_score_beacon->expiration_time = -1;
        lower_score_beacon->next_initiation_time = old_beacon->initiation_time;
        lower_score_beacon->next_expiration_time = old_beacon->expiration_time;
        lower_score_beacon->is_new = true;
        lower_score_beacon->is_valid = false;
        lower_score_beacon->bwd_stat = bwd;
        lower_score_beacon->latency_stat = latency;
        path_len = lower_score_beacon->the_path->size();

        remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, lower_score_beacon));
        remote_as->path_map_to_beacon.insert(std::make_pair(key, lower_score_beacon));

        if (remote_as->beacon_store.at(src_as)->find(path_len) != remote_as->beacon_store.at(src_as)->end()) {
            remote_as->beacon_store.at(src_as)->at(path_len)->insert(lower_score_beacon);
        } else {
            remote_as->beacon_store.at(src_as)->insert(std::make_pair(path_len, new beacons_with_equal_length()));
            remote_as->beacon_store.at(src_as)->at(path_len)->insert(lower_score_beacon);
        }
    }
}

void CriteriaMatching::UpdateSpecializedBeaconStore(SCION_Node* remote_as, ld latency, ld bwd, uint16_t src_as_no, beacon *new_beacon){
    ld score = CalculateBeaconScore(remote_as, latency, bwd);
    if (remote_as->beacons_sorted_by_score.find(src_as_no) != remote_as->beacons_sorted_by_score.end()) {
        remote_as->beacons_sorted_by_score.at(src_as_no)->insert(std::make_pair(score, new_beacon));
    } else {
        remote_as->beacons_sorted_by_score.insert(std::make_pair(src_as_no, new std::multimap<ld, beacon*> ()));
        remote_as->beacons_sorted_by_score.at(src_as_no)->insert(std::make_pair(score, new_beacon));
    }
}