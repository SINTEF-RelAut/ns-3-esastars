//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BASELINE_H
#define SCION_BEACONING_SIMMULATOR_BASELINE_H
#include "beaconing_strategy.h"
class Baseline : public BeaconingStrategy{
public:
    virtual void InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node);
    virtual void DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node);
    virtual void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces,SCION_Node* node);
private:
    void UpdateBeaconStoreAndCountersBeforeBeaconing();
    void send_beacon_to_all_interfaces_with_same_remote_as(beacon* the_beacon, uint16_t remote_as_no);
    void GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no,
            uint16_t remote_ingress_if_no, ns3::Ptr<SCION_Node> remote_as, ld latency, ld bwd,
            bool immediate, ld latency_for_immediate);
};
#endif //SCION_BEACONING_SIMMULATOR_BASELINE_H
