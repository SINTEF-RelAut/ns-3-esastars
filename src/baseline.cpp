//
// Created by chrissy on 10.06.20.
//
#include <omp.h>
#include "../headers/baseline.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

void Baseline::DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, ns3::Ptr<SCION_Node> node){
    // TODO: Debugg
    //std::cerr << "Size(valid_interfaces) node_nr -> vector<intf_no>:" << std::endl;
    //std::cerr << valid_interfaces.size() << std::endl;
    // TODO: Change back
//#pragma omp parallel for
    for (uint32_t i = 0; i < node->neighbors.size(); ++i){
        uint16_t remote_as_no = node->neighbors.at(i);
        std::vector<uint16_t> interfaces = valid_interfaces.at(remote_as_no);
        // TODO: Figure out why it is complaining about structured bindings and change back..
        for (auto const it: node->beacon_store) { // Per source AS
            auto src_as_no = it.first;
            auto equal_src_as_beacons = it.second;
            int16_t  sent_count = 0;

            if (remote_as_no == src_as_no) {
                continue;
            }

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

                    // TODO: Debugg
                    //std::cerr << "Size(interfaces):" << std::endl;
                    //std::cerr << interfaces.size() << std::endl;
                    // Iterate over all the valid interfaces of this remote AS and send the beacons
                    for (auto egress_interface_no: interfaces){
                        ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = ns3::DynamicCast<ns3::PointToPointNetDevice>(node->GetDevice(egress_interface_no));

                        auto [remote_ingress_if_no, remote_as] = GetRemoteAsInfo(node, egress_interface_no);

                        ld latency = the_beacon->latency_stat + node->intra_as_latencies.at(the_beacon->the_path->back()[3]).at(egress_interface_no);
                        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(egress_interface_no)
                                 ? (ld) node->inter_as_bwds.at(egress_interface_no)
                                 : the_beacon->bwd_stat;


                        GenerateBeaconAndSend(the_beacon, egress_interface_no, remote_as_no, remote_ingress_if_no, node,
                                              remote_as, latency, bwd, false, 0.0);
                    }
                }
            }
        }
    }
}

void Baseline::HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                             ns3::Ptr<SCION_Node> node, ns3::Ptr<SCION_Node> remote_as,
                                             ld latency, ld bwd) {
    // In this case, we don't evict any beacons but simply ignore the new one
    return;
}

void Baseline::UpdateSpecializedBeaconStore(ns3::Ptr<SCION_Node> remote_as, ld latency, ld bwd, uint16_t src_as_no,
                                  beacon *new_beacon){
    // We do not use a specialized beacon store structure for this strategy
    return;
}