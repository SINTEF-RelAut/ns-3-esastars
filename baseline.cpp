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

                    // Iterate over all the valid interfaces of this remote AS and send the beacons
                    for (auto egress_interface_no: interfaces){
                        ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = DynamicCast<ns3::PointToPointNetDevice>(node->GetDevice(egress_interface_no));

                        ns3::Ptr<ns3::PointToPointChannel> channel = DynamicCast<ns3::PointToPointChannel>(self_egress_device->GetChannel());
                        uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
                        ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

                        uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();

                        ns3::Ptr<SCION_Node> remote_as = (DynamicCast<SCION_Node>(remote_device->GetNode()));

                        ld latency = the_beacon->latency_stat + node->intra_as_latencies.at(the_beacon->the_path->back()[3]).at(egress_interface_no);
                        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(egress_interface_no)
                                 ? (ld) node->inter_as_bwds.at(egress_interface_no)
                                 : the_beacon->bwd_stat;


                        GenerateBeaconAndSend(the_beacon, egress_interface_no, remote_as_no, remote_ingress_if_no,
                                              remote_as, latency, bwd, false, 0.0);
                    }
                }
            }
        }
    }
}

void Baseline::GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                      SCION_Node* node, ns3::Ptr<SCION_Node> remote_as,
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
    } else {
        key = old_beacon->key;
        src_as = *old_beacon->the_path->at(0);
        int64_t t = node->now - node->now % 600000000000;
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (70 + 330 + 330 * old_beacon->the_path->size());
    }

    // *** For immediately disseminating beacons received from neighbor source as
    if (old_beacon == NULL
        && remote_as->valid_beacons_count_per_src_as.find(src_as) == remote_as->valid_beacons_count_per_src_as.end()
        && remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) == remote_as->next_round_valid_beacons_count_per_src_as.end()) {
        immediate_src = true;

    }

    if (immediate) {
        if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) == remote_as->next_round_valid_beacons_count_per_src_as.end() ||
            remote_as->next_round_valid_beacons_count_per_src_as.at(src_as) < 5) {
            immediate_non_src = true;
        }
    }
    // ***

    key = key + std::string((char *) &node->as_number, 2) + std::string((char *) &self_egress_if_no, 2);

    if (remote_as->path_map_to_beacon.find(key) != remote_as->path_map_to_beacon.end()) {
        if (old_beacon == NULL) {
            remote_as->path_map_to_beacon.at(key)->next_initiation_time = now;
            remote_as->path_map_to_beacon.at(key)->next_expiration_time = now + expiration_period;
        } else {
            remote_as->path_map_to_beacon.at(key)->next_initiation_time = old_beacon->initiation_time;
            remote_as->path_map_to_beacon.at(key)->next_expiration_time = old_beacon->expiration_time;
        }
        remote_as->path_map_to_beacon.at(key)->is_new = true;
        return;
    }

    if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as) !=
        remote_as->next_round_valid_beacons_count_per_src_as.end()) {
        if (remote_as->next_round_valid_beacons_count_per_src_as.at(src_as) >= FIXED_BEACONS_NUMBER_TO_STORE) {
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

    uint16_t *link_info = new uint16_t[4];
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
    remote_as->path_map_to_beacon.insert(std::make_pair(key, new_beacon));
    uint16_t path_len = new_path->size();

    if (remote_as->beacon_store.find(src_as) != remote_as->beacon_store.end() &&
        remote_as->beacon_store.at(src_as)->find(path_len) != remote_as->beacon_store.at(src_as)->end()) {
        // TODO: Unification
        remote_as->beacon_store.at(src_as)->at(path_len)->push_back(new_beacon);
    } else if (remote_as->beacon_store.find(src_as) != remote_as->beacon_store.end() &&
               remote_as->beacon_store.at(src_as)->find(path_len) == remote_as->beacon_store.at(src_as)->end()) {
        // TODO: Unification
        remote_as->beacon_store.at(src_as)->insert(std::make_pair(path_len, new beacons_with_equal_length(1, new_beacon)));

    } else {
        remote_as->beacon_store.insert(std::make_pair(src_as, new beacons_with_same_src_as));
        // TODO: Unification
        remote_as->beacon_store.at(src_as)->insert(std::make_pair(path_len, new beacons_with_equal_length(1, new_beacon)));
    }

    if (immediate_src) {
        // TODO: Call right function once you have implemented the core & non-core AS classes (type of remote AS!)
        // virtual void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) = 0;
        ns3::Simulator::Schedule(ns3::MilliSeconds(1), &myNode::processImmediateReceive, remote_as, src_as, remote_ingress_if_no, new_beacon);
    }

    if (immediate_non_src) {
        uint64_t delay = (uint64_t) (latency_for_immediate * 1000000);
        // TODO: Call right function once you have implemented the core & non-core AS classes
        ns3::Simulator::Schedule(ns3::NanoSeconds(delay), &myNode::processImmediateReceive, remote_as, src_as, remote_ingress_if_no, new_beacon);
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

