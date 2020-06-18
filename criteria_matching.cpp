//
// Created by chrissy on 10.06.20.
//
#include "criteria_matching.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
void CriteriaMatching::DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
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
                                     SCION_Node* node, ns3::Ptr<SCION_Node> remote_as,
                                     ld latency, ld bwd, bool immediate, ld latency_for_immediate) {
}

void CriteriaMatching::processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node){
}


