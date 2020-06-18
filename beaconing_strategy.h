//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
#define SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H

#include "beacon.h"
#include "scion_node.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

class BeaconingStrategy{
public:
    virtual void DisseminateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node) = 0;
    void InitiateBeacons(std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces, SCION_Node* node);
    void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, std::unordered_map<uint16_t, std::vector<uint16_t>> valid_interfaces,SCION_Node* node);

protected:
    virtual void GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                       SCION_Node* node, ns3::Ptr<SCION_Node> remote_as,
                                       ld latency, ld bwd, bool immediate, ld latency_for_immediate) = 0;
    static void AdjustBeaconValidity(beacon* the_beacon, SCION_Node *node);

    static void DoBeaconing(SCION_Node* node);
    static bool generates_loop(beacon const* the_beacon, uint16_t remote_as_no);
    static void UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node* node);
    static std::tuple<uint16_t, ns3::Ptr<SCION_Node>> GetRemoteAsInfo(SCION_Node *node, uint16_t egress_interface_no);
};
#endif //SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
