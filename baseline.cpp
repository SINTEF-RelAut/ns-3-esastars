//
// Created by chrissy on 10.06.20.
//

#include "baseline.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

void Baseline::InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
    for (auto const& [remote_as_no, interfaces]: valid_interfaces){
        for (auto const & self_egress_if_no : interfaces) {
            ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = DynamicCast<ns3::PointToPointNetDevice>(
                    node->GetDevice(self_egress_if_no));
            ns3::Ptr<ns3::PointToPointChannel> channel = DynamicCast<ns3::PointToPointChannel>(
                    self_egress_device->GetChannel());
            uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
            ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

            uint16_t remote_if_no = (uint16_t) remote_device->GetIfIndex();

            ns3::Ptr<SCION_Node> remote_as = (DynamicCast<SCION_Node>(remote_device->GetNode()));

            GenerateBeaconAndSend(NULL, self_egress_if_no, remote_as_no, remote_if_no, remote_as,
                                  0.0, node->inter_as_bwds.at(self_egress_if_no), false, 0.0);
        }

    }
}

void Baseline::DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
#pragma omp parallel for
    for (auto const& [remote_as_no, interfaces]: valid_interfaces){
        for (auto const &beacon_store_entry : node->beacon_store) { // Per source AS
            uint16_t src_as_no = beacon_store_entry.first;
            beacons_with_same_src_as *equal_src_as_beacons = beacon_store_entry.second;

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

                    this->send_beacon_to_all_interfaces_with_same_remote_as(the_beacon, remote_as_no);
                }
            }
        }
    }
}

void Baseline::processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node){
    if (node->valid_beacons_count_per_src_as.find(src_as_no) != node->valid_beacons_count_per_src_as.end()) {
        return; // only process unknown beacons immediately
    }

    AdjustBeaconValidity(the_beacon, node);

    for (auto const& [dst_as_no, interfaces]: valid_interfaces){
        if (dst_as_no == src_as_no) {
            continue;
        }

        uint16_t min_egress_if = 0;
        ld min_latency = std::numeric_limits<double>::max();

        for (auto const & egress_if : interfaces) {
            if (node->intra_as_latencies.at(ingress_if).at(egress_if) < min_latency) {
                min_latency = node->intra_as_latencies.at(ingress_if).at(egress_if);
                min_egress_if = egress_if;
            }
        }

        ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = DynamicCast<ns3::PointToPointNetDevice>(
                node->GetDevice(min_egress_if));

        ns3::Ptr<ns3::PointToPointChannel> channel = DynamicCast<ns3::PointToPointChannel>(
                self_egress_device->GetChannel());
        uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
        ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

        uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();

        ns3::Ptr<SCION_Node> remote_as = (DynamicCast<SCION_Node>(remote_device->GetNode()));

        ld latency = the_beacon->latency_stat
                     + node->intra_as_latencies.at(ingress_if).at(min_egress_if);
        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(min_egress_if)
                 ? (ld) node->inter_as_bwds.at(min_egress_if)
                 : the_beacon->bwd_stat;

        GenerateBeaconAndSend(the_beacon, min_egress_if, dst_as_no, remote_ingress_if_no,
                              remote_as, latency, bwd, true, min_latency);

    }
}

void Baseline::UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node* node) {
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);

    if (node->as_number == 0) { // TODO: Move this to avoid race condition
        std::cout << "################################## " << node->now << " #########################################" << std::endl;
    }

    for (auto const &pair:node->path_map_to_beacon) {
        beacon *the_beacon = pair.second;
        AdjustBeaconValidity(the_beacon, node);
    }

}
