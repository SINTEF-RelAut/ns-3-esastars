//
// Created by chrissy on 10.06.20.
//

#include "ns3/point-to-point-net-device.h"
#include "../headers/beaconing_strategy.h"
#include "../headers/utils.h"
#include "ns3/ptr.h"

void BeaconingStrategy::InitiateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node){
    for (auto const& [remote_as_no, interfaces]: valid_interfaces){
        for (auto const & self_egress_if_no : interfaces) {
            auto [remote_if_no, remote_as_ptr] = GetRemoteAsInfo(node, self_egress_if_no);

            SCION_Node* remote_as = ns3::GetPointer(remote_as_ptr);

            GenerateBeaconAndSend(NULL, self_egress_if_no, remote_as_no, remote_if_no, node,remote_as,
                                  0.0, node->inter_as_bwds.at(self_egress_if_no), false, 0.0);

            // remote_as goes out of scope
            remote_as_ptr->Unref();
        }

    }
}

void BeaconingStrategy::AdjustBeaconValidity(beacon* the_beacon, SCION_Node* node){
    node->now = ns3::Simulator::Now().ToInteger(ns3::Time::NS);

    uint16_t src_as = the_beacon->the_path->at(0)[0];
    if (the_beacon->is_new) {
        the_beacon->is_new = false;

        if (the_beacon->next_expiration_time > node->now) {
            if (!the_beacon->is_valid) {
                if(node->valid_beacons_count_per_src_as.find(src_as) != node->valid_beacons_count_per_src_as.end()){
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

bool BeaconingStrategy::generates_loop(beacon const* the_beacon, uint16_t remote_as_no){
    for (auto const &link_info : *the_beacon->the_path) { // remove loops
        if (link_info[0] == remote_as_no) {
            return true;
        }
    }
    return false;
}

void BeaconingStrategy::UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node* node){

    for (auto const &the_beacon_pair:node->path_map_to_beacon) {
        beacon* the_beacon = the_beacon_pair.second;
        AdjustBeaconValidity(the_beacon, node);
    }
}

std::pair<uint16_t, ns3::Ptr<SCION_Node>> BeaconingStrategy::GetRemoteAsInfo(SCION_Node* node, uint16_t egress_interface_no){
    ns3::Ptr<ns3::PointToPointNetDevice> self_egress_device = ns3::DynamicCast<ns3::PointToPointNetDevice>(node->GetDevice(egress_interface_no));

    ns3::Ptr<ns3::PointToPointChannel> channel = ns3::DynamicCast<ns3::PointToPointChannel>(self_egress_device->GetChannel());
    uint32_t wire = self_egress_device == channel->GetSource(0) ? 0 : 1;
    ns3::Ptr<ns3::PointToPointNetDevice> remote_device = channel->GetDestination(wire);

    uint16_t remote_ingress_if_no = (uint16_t) remote_device->GetIfIndex();

    ns3::Ptr<SCION_Node> remote_as = (ns3::DynamicCast<SCION_Node>(remote_device->GetNode()));

    return std::make_pair(remote_ingress_if_no, remote_as);
}

void BeaconingStrategy::processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node){
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

        auto [remote_ingress_if_no, remote_as_ptr] = GetRemoteAsInfo(node, min_egress_if);
        SCION_Node* remote_as = ns3::GetPointer(remote_as_ptr);
        ld latency = the_beacon->latency_stat
                     + node->intra_as_latencies.at(ingress_if).at(min_egress_if);
        ld bwd = the_beacon->bwd_stat > (ld) node->inter_as_bwds.at(min_egress_if)
                 ? (ld) node->inter_as_bwds.at(min_egress_if)
                 : the_beacon->bwd_stat;

        GenerateBeaconAndSend(the_beacon, min_egress_if, dst_as_no, remote_ingress_if_no, node,
                              remote_as, latency, bwd, true, min_latency);
        // remote_as goes out of scope
        remote_as_ptr->Unref();
    }
}

void BeaconingStrategy::GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                             SCION_Node* node, SCION_Node* remote_as,
                                             ld latency, ld bwd, bool immediate, ld latency_for_immediate) {
    uint16_t src_as_no;
    std::string key;

    bool immediate_src = false;
    bool immediate_non_src = false;

    if (old_beacon == NULL) {
        src_as_no = node->as_number;
        // TODO: Have some descriptive constants somewhere
        int64_t t = node->now - node->now % 600000000000; // 600s? ~ 10h
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (70 + 330);
        // TODO: Debugg
        if(t == 0) {
            std::cerr << "in GB&S: " << std::endl;
            print_consumed_bw_structure(node);
        }
        // *** For immediately disseminating beacons received from neighbor source as // TODO: double check this. Was remote as modified before this check?
        if(remote_as->valid_beacons_count_per_src_as.find(src_as_no) == remote_as->valid_beacons_count_per_src_as.end()
           && remote_as->next_round_valid_beacons_count_per_src_as.find(src_as_no) == remote_as->next_round_valid_beacons_count_per_src_as.end()){
            // Remote as not found in any beacon store. TODO: Should this really be dependent on the next_round store as well?
            immediate_src = true;
        }
    } else {
        key = old_beacon->key;
        src_as_no = *old_beacon->the_path->at(0);
        int64_t t = node->now - node->now % 600000000000;
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (70 + 330 + 330 * old_beacon->the_path->size());
    }

    //TODO: Does it make sense to choose how to disseminate based on the remote_ases beacon store? What does this model in the real deployment?
    if (immediate) {
        // src_AS_no not found in next_round beacon store. Or less than 5 beacons in next round store from this AS.
        // TODO: Why is this not dependent on the current beacon store like above?
        if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as_no) == remote_as->next_round_valid_beacons_count_per_src_as.end() ||
            remote_as->next_round_valid_beacons_count_per_src_as.at(src_as_no) < MAX_IMMEDIATE_BEACONS) {
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

    // Update statistics & check if you are sending too many beacons
    if (remote_as->next_round_valid_beacons_count_per_src_as.find(src_as_no) !=
        remote_as->next_round_valid_beacons_count_per_src_as.end()) {
        if (remote_as->next_round_valid_beacons_count_per_src_as.at(src_as_no) >= FIXED_BEACONS_NUMBER_TO_STORE) { // TODO: There seems to be a mismatch here? next round vs storing?
            HandleFullBeaconStore(key, src_as_no, old_beacon, self_egress_if_no, remote_as_no, remote_ingress_if_no, node, remote_as, latency, bwd);
            return; // If the beacon store was full, we are done after this call.
        }
        remote_as->next_round_valid_beacons_count_per_src_as.at(src_as_no)++;
    } else {
        remote_as->next_round_valid_beacons_count_per_src_as.insert(std::make_pair(src_as_no, 1));
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
    uint16_t path_len = new_beacon->the_path->size();
    // Here we can be sure, that the beacon is not in the path map yet (checked before).
    remote_as->path_map_to_beacon.insert(std::make_pair(key, new_beacon));

    if (remote_as->beacon_store.find(src_as_no) != remote_as->beacon_store.end()){
        if (remote_as->beacon_store.at(src_as_no)->find(path_len) != remote_as->beacon_store.at(src_as_no)->end()){
            remote_as->beacon_store.at(src_as_no)->at(path_len)->insert(new_beacon);
        } else{
            remote_as->beacon_store.at(src_as_no)->insert(std::make_pair(path_len, new beacons_with_equal_length ({new_beacon})));
        }
    } else {
        remote_as->beacon_store.insert(std::make_pair(src_as_no, new equal_as_beacons_sorted_by_length));
        remote_as->beacon_store.at(src_as_no)->insert(std::make_pair(path_len, new beacons_with_equal_length({new_beacon})));
    }

    UpdateSpecializedBeaconStore(remote_as, latency, bwd, src_as_no, new_beacon);

    if (immediate_src) {
        ns3::Simulator::Schedule(ns3::MilliSeconds(1), &SCION_Node::ProcessReceivedBeacons, remote_as, src_as_no, remote_ingress_if_no, new_beacon);
    }

    if (immediate_non_src) {
        uint64_t delay = (uint64_t) (latency_for_immediate * 1000000);
        ns3::Simulator::Schedule(ns3::NanoSeconds(delay), &SCION_Node::ProcessReceivedBeacons, remote_as, src_as_no, remote_ingress_if_no, new_beacon);
    }

}