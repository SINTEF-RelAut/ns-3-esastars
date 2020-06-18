//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BASELINE_H
#define SCION_BEACONING_SIMMULATOR_BASELINE_H
#include "beaconing_strategy.h"
class Baseline : public BeaconingStrategy{
public:
    void DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) override;
protected:
    void GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                               SCION_Node* node, ns3::Ptr<SCION_Node> remote_as,
                               ld latency, ld bwd, bool immediate, ld latency_for_immediate) override;
};
#endif //SCION_BEACONING_SIMMULATOR_BASELINE_H
