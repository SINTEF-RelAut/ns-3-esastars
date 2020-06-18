//
// Created by chrissy on 10.06.20.
//

#include <ns3/point-to-point-net-device.h>
#include "beaconing_strategy.h"
#include "ns3/ptr.h"

void BeaconingStrategy::InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
    for (auto const& [remote_as_no, interfaces]: valid_interfaces){
        for (auto const & self_egress_if_no : interfaces) {
            ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = ns3::DynamicCast<ns3::PointToPointNetDevice>(
                    node->GetDevice(self_egress_if_no));
            ns3::Ptr<ns3::PointToPointChannel> channel = ns3::DynamicCast<ns3::PointToPointChannel>(
                    self_egress_device->GetChannel());
            uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
            ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

            uint16_t remote_if_no = (uint16_t) remote_device->GetIfIndex();

            ns3::Ptr<SCION_Node> remote_as = (ns3::DynamicCast<SCION_Node>(remote_device->GetNode()));

            GenerateBeaconAndSend(NULL, self_egress_if_no, remote_as_no, remote_if_no, node,remote_as,
                                  0.0, node->inter_as_bwds.at(self_egress_if_no), false, 0.0);
        }

    }
}

void BeaconingStrategy::AdjustBeaconValidity(beacon* the_beacon, SCION_Node *node){
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);

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

        if (node->valid_beacons_count_per_src_as.at(src_as) == 0) {
            node->valid_beacons_count_per_src_as.erase(src_as);
        }

        if (node->next_round_valid_beacons_count_per_src_as.at(src_as) == 0) {
            node->next_round_valid_beacons_count_per_src_as.erase(src_as);
        }
    }

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

void BeaconingStrategy::UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node *node){
    uint16_t src_as = node->as_number;

    if (src_as == 0) { // TODO: Move one lvl up to avoid race condition
        std::cout << "################################## " << node->now << " #########################################" << std::endl;
    }

    for (auto const &the_beacon_pair:node->path_map_to_beacon) {
        beacon* the_beacon = the_beacon_pair.second;
        AdjustBeaconValidity(the_beacon, node);
    }

    std::cout << node->as_number << "\t" <<node->valid_beacons_count_per_src_as.size() << std::endl; // Print number of source ASes
}

std::tuple<uint16_t, ns3::Ptr<SCION_Node>> BeaconingStrategy::GetRemoteAsInfo(SCION_Node *node, uint16_t egress_interface_no){
    ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = ns3::DynamicCast<ns3::PointToPointNetDevice>(node->GetDevice(egress_interface_no));

    ns3::Ptr<ns3::PointToPointChannel> channel = ns3::DynamicCast<ns3::PointToPointChannel>(self_egress_device->GetChannel());
    uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
    ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

    uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();

    ns3::Ptr<SCION_Node> remote_as = (ns3::DynamicCast<SCION_Node>(remote_device->GetNode()));

    return std::make_tuple(remote_ingress_if_no, remote_as);
}

void BeaconingStrategy::processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node){
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

        auto [remote_ingress_if_no, remote_as] = GetRemoteAsInfo(node, min_egress_if);

        ld latency = the_beacon->latency_stat
                     + node->intra_as_latencies.at(ingress_if).at(min_egress_if);
        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(min_egress_if)
                 ? (ld) node->inter_as_bwds.at(min_egress_if)
                 : the_beacon->bwd_stat;

        GenerateBeaconAndSend(the_beacon, min_egress_if, dst_as_no, remote_ingress_if_no, node,
                              remote_as, latency, bwd, true, min_latency);

    }
}
