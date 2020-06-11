//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
#define SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H

#include "beacon.h"
#include "scion_node.h"

class BeaconingStrategy{
public:
    virtual void InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) = 0;
    virtual void DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) = 0;
    virtual void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) = 0;

protected:
    static void DoBeaconing(SCION_Node* node);
    static bool generates_loop(beacon const* the_beacon, uint16_t remote_as_no);
    static void UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node* node);
};
#endif //SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
