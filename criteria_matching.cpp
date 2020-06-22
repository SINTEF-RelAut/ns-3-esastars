//
// Created by chrissy on 10.06.20.
//
#include "criteria_matching.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-channel.h"

void CriteriaMatching::DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, ns3::Ptr<SCION_Node> node){
#pragma omp parallel for
    for (auto const& [remote_as_no, interfaces]: valid_interfaces){
        for (auto const [src_as_no, equal_src_as_beacons]: node->beacon_store) { // Per source AS
            if (remote_as_no == src_as_no) {
                continue;
            }
            std::multimap<int64_t, std::tuple<beacon*, uint16_t, uint16_t, ns3::Ptr<SCION_Node>, ld , ld> > beacons_ifaces_matchings_scores;
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

                        auto [remote_ingress_if_no, remote_as] = GetRemoteAsInfo(node, egress_interface_no);

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

                        beacons_ifaces_matchings_scores.insert(std::make_pair(score, std::tuple<beacon *, uint16_t, uint16_t, ns3::Ptr<SCION_Node>, ld,
                                                                              ld>(the_beacon, egress_interface_no, remote_ingress_if_no, remote_as, latency, bwd)));

                        if (beacons_ifaces_matchings_scores.size() > FIXED_BEACONS_NUMBER_TO_SEND) {
                            beacons_ifaces_matchings_scores.erase(beacons_ifaces_matchings_scores.begin());
                        }
                    }
                }
            }

            for (auto const &the_tuple_pair : beacons_ifaces_matchings_scores) {
                beacon *the_beacon;
                uint16_t remote_ingress_if_no;
                uint16_t egress_interface_no;
                ns3::Ptr<SCION_Node> remote_as;
                ld latency;
                ld bwd;

                std::tie(the_beacon, egress_interface_no, remote_ingress_if_no, remote_as, latency, bwd) = the_tuple_pair.second;

                GenerateBeaconAndSend(the_beacon, egress_interface_no, remote_as_no, remote_ingress_if_no, node,
                                      remote_as, latency, bwd, false, 0.0);

            }

        }
    }
}

void CriteriaMatching::GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                             ns3::Ptr<SCION_Node> node, ns3::Ptr<SCION_Node> remote_as,
                                     ld latency, ld bwd, bool immediate, ld latency_for_immediate) {
    uint16_t src_as;
    std::string key;

    bool immediate_src = false;
    bool immediate_non_src = false;

    if (old_beacon == NULL) {
        src_as = node->as_number;
        // TODO: Have some descriptive constants somewhere
        int64_t t = node->now - node->now % 600000000000;
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (70 + 330);
        // *** For immediately disseminating beacons received from neighbor source as // TODO: double check this. Was remote as modified before this check?
        if(remote_as->valid_beacons_count_per_src_as.find(src_as) == remote_as->valid_beacons_count_per_src_as.end()
           && remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) == remote_as->next_round_valid_beacons_count_per_src_as.end()){
            // Remote as not found in any beacon store. TODO: Should this really be dependent on the next_round store as well?
            immediate_src = true;
        }
    } else {
        key = old_beacon->key;
        src_as = *old_beacon->the_path->at(0);
        int64_t t = node->now - node->now % 600000000000;
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (70 + 330 + 330 * old_beacon->the_path->size());
    }

    //TODO: Does it make sense to choose how to disseminate based on the remote_ases beacon store? What does this model in the real deployment?
    if (immediate) {
        // src_AS_no not found in next_round beacon store. Or less than 5 beacons in next round store from this AS.
        // TODO: Why is this not dependent on the current beacon store like above?
        if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) == remote_as->next_round_valid_beacons_count_per_src_as.end() ||
            remote_as->next_round_valid_beacons_count_per_src_as.at(src_as) < 5) { // TODO: constant
            immediate_non_src = true;
        }
    }
    // ***

    key = key + std::string((char *) &node->as_number, 2) + std::string((char *) &self_egress_if_no, 2);

    // If the beacon is already in the remote_ases beacon store // TODO: Why is this check needed?
    if (remote_as->path_map_to_beacon.find(key) != remote_as->path_map_to_beacon.end()) {
        if (old_beacon == NULL) {
            remote_as->path_map_to_beacon.at(key)->next_initiation_time = node->now;
            remote_as->path_map_to_beacon.at(key)->next_expiration_time = node->now + node->expiration_period;
        } else {
            remote_as->path_map_to_beacon.at(key)->next_initiation_time = old_beacon->initiation_time;
            remote_as->path_map_to_beacon.at(key)->next_expiration_time = old_beacon->expiration_time;
        }
        remote_as->path_map_to_beacon.at(key)->is_new = true;
        return;
    }

    // TODO: From start until here, the functions are identical => make a common function in base class?
    // Might get a bit spaghetticody because of return? Would have to make an if-else block out of it.

    ld score = ((1 - latency / 1000) * remote_as->latency_coef + (bwd / 400) * remote_as->bandwidth_coef)
               / (remote_as->latency_coef + remote_as->bandwidth_coef);


    // Update statistics & check if you are sending too many beacons
    if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) !=
        remote_as->next_round_valid_beacons_count_per_src_as.end()) {
        if (remote_as->next_round_valid_beacons_count_per_src_as.at(src_as) >= FIXED_BEACONS_NUMBER_TO_STORE) { // TODO: There seems to be a mismatch here? next round vs storing?
            std::multimap <ld, beacon* >::iterator it = remote_as->beacons_sorted_by_score.at(src_as)->begin();
            if (it->first < score) {
                beacon* lower_score_beacon = it->second;
                remote_as->beacons_sorted_by_score.at(src_as)->erase(it);
                remote_as->path_map_to_beacon.erase(lower_score_beacon->key);
                remote_as->beacon_store.at(src_as)->at(lower_score_beacon->the_path->back()[0])->erase(lower_score_beacon);
                if (remote_as->beacon_store.at(src_as)->at(lower_score_beacon->the_path->back()[0])->empty()) {
                    remote_as->beacon_store.at(src_as)->erase(lower_score_beacon->the_path->back()[0]);
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

                remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, lower_score_beacon));
                remote_as->path_map_to_beacon.insert(std::make_pair(key, lower_score_beacon));

                if (remote_as->beacon_store.at(src_as)->find(node->as_number) != remote_as->beacon_store.at(src_as)->end()) {
                    remote_as->beacon_store.at(src_as)->at(node->as_number)->insert(lower_score_beacon);
                } else {
                    remote_as->beacon_store.at(src_as)->insert(std::make_pair(node->as_number, new beacons_with_equal_length()));
                    remote_as->beacon_store.at(src_as)->at(node->as_number)->insert(lower_score_beacon);
                }
                return;
            }
            return;
        }
        remote_as->next_round_valid_beacons_count_per_src_as.at(src_as)++;
    } else {
        remote_as->next_round_valid_beacons_count_per_src_as.insert(std::make_pair(src_as, 1));
    }


    beacon *new_beacon = new beacon;
    path *new_path = new path;
    new_beacon->the_path = new_path;
    new_beacon->bwd_stat = bwd;
    new_beacon->latency_stat = latency;

    uint16_t *link_info = new uint16_t[4]; // TODO: Actually use the typedef you created for this.
    link_info[0] = node->as_number;
    link_info[1] = self_egress_if_no;
    link_info[2] = remote_as_no;
    link_info[3] = remote_ingress_if_no;

    new_beacon->initiation_time = -1;
    new_beacon->expiration_time = -1;
    new_beacon->key = key;
    new_beacon->is_new = true;
    new_beacon->is_valid = false;

    if (old_beacon == NULL) {
        new_beacon->next_initiation_time = node->now;
        new_beacon->next_expiration_time = node->now + node->expiration_period;
    } else {
        new_beacon->next_initiation_time = old_beacon->initiation_time;
        new_beacon->next_expiration_time = old_beacon->expiration_time;

        *new_path = *(old_beacon->the_path);
    }

    new_path->push_back(link_info);
    // Here we can be sure, that the beacon is not in the path map yet (checked before).
    remote_as->path_map_to_beacon.insert(std::make_pair(key, new_beacon));

    // TODO: from "beacon *new_beacon = new beacon;" until here functions are identical again

    if (remote_as->beacon_store.find(src_as) != remote_as->beacon_store.end()){
        if (remote_as->beacon_store.at(src_as)->find(node->as_number) != remote_as->beacon_store.at(src_as)->end()){
            remote_as->beacon_store.at(src_as)->at(node->as_number)->insert(new_beacon);
        } else{
            remote_as->beacon_store.at(src_as)->insert(std::make_pair(node->as_number, new beacons_with_equal_length ()));
            remote_as->beacon_store.at(src_as)->at(node->as_number)->insert(new_beacon);
        }
    } else {
        remote_as->beacon_store.insert(std::make_pair(src_as, new beacons_with_same_src_as));
        remote_as->beacon_store.at(src_as)->insert(std::make_pair(node->as_number, new beacons_with_equal_length()));
        remote_as->beacon_store.at(src_as)->at(node->as_number)->insert(new_beacon);
    }

    if (remote_as->beacons_sorted_by_score.find(src_as) != remote_as->beacons_sorted_by_score.end()) {
        remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, new_beacon));
    } else {
        remote_as->beacons_sorted_by_score.insert(std::make_pair(src_as, new std::multimap<ld, beacon*> ()));
        remote_as->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, new_beacon));
    }

    if (immediate_src) {
        ns3::Simulator::Schedule(ns3::MilliSeconds(1), &SCION_Node::ProcessReceivedBeacons, remote_as, src_as, remote_ingress_if_no, new_beacon);
    }

    if (immediate_non_src) {
        uint64_t delay = (uint64_t) (latency_for_immediate * 1000000);
        ns3::Simulator::Schedule(ns3::NanoSeconds(delay), &SCION_Node::ProcessReceivedBeacons, remote_as, src_as, remote_ingress_if_no, new_beacon);
    }

}


