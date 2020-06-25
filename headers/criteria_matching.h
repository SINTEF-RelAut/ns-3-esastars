//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#define SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#include "beaconing_strategy.h"
class CriteriaMatching : public BeaconingStrategy {
public:
    void DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces,
                            SCION_Node* node) override;

protected:
    void HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no,
                               uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                               SCION_Node* node, SCION_Node* remote_as,
                               ld latency, ld bwd) override;

    void UpdateSpecializedBeaconStore(SCION_Node* remote_as, ld latency, ld bwd, uint16_t src_as_no,
                                      beacon *new_beacon) override;

private:
    static ld CalculateBeaconScore(SCION_Node* remote_as, ld latency, ld bwd);
};
#endif //SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
