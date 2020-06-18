//
// Created by chrissy on 10.06.20.
//

#include "criteria_matching.h"
void CriteriaMatching::InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
}

void CriteriaMatching::DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node *node){
}

void CriteriaMatching::GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                     SCION_Node* node, ns3::Ptr<SCION_Node> remote_as,
                                     ld latency, ld bwd, bool immediate, ld latency_for_immediate) {
}

void CriteriaMatching::processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node){
}


