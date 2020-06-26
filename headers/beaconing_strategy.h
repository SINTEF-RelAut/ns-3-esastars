//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
#define SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H

#include <unordered_map>
#include "beacon.h"
#include "scion_node.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"

// TODO: Consider moving all the constants and macros into a config file?
const uint8_t MAX_IMMEDIATE_BEACONS = 5;

class BeaconingStrategy{
public:
    void InitiateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node);
    void processImmediateReceive(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon, const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node);
    static void UpdateBeaconStoreAndCountersBeforeBeaconing(SCION_Node* node);
    virtual void DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node) = 0;

protected:
    static void AdjustBeaconValidity(beacon* the_beacon, SCION_Node* node);

    static bool generates_loop(beacon const* the_beacon, uint16_t remote_as_no);
    static std::pair<uint16_t, ns3::Ptr<SCION_Node>> GetRemoteAsInfo(SCION_Node* node, uint16_t egress_interface_no);

    void GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                               SCION_Node* node, SCION_Node* remote_as,
                                       ld latency, ld bwd, bool immediate, ld latency_for_immediate);
    // TODO: Better name?
    virtual void HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                                       SCION_Node* node, SCION_Node* remote_as,
                                       ld latency, ld bwd) = 0;
    virtual void UpdateSpecializedBeaconStore(SCION_Node* remote_as, ld latency, ld bwd, uint16_t src_as_no, beacon *new_beacon) = 0;

};
#endif //SCION_BEACONING_SIMMULATOR_BEACONING_STRATEGY_H
