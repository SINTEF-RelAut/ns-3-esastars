//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#define SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#include "beaconing_strategy.h"
class CriteriaMatching : public BeaconingStrategy{
public:
    void InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) override;
    void DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) override;
    void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces,SCION_Node* node) override;
private:
    void GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                      SCION_Node* node, ns3::Ptr<SCION_Node> remote_as,
                                      ld latency, ld bwd, bool immediate, ld latency_for_immediate) override;
};
#endif //SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
