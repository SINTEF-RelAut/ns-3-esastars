/**
 * @file baseline.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Defines no-ops for the baseline beaconing strategy.
 */

#ifndef SCION_BEACONING_SIMMULATOR_BASELINE_H
#define SCION_BEACONING_SIMMULATOR_BASELINE_H
#include "beaconing_strategy.h"
class Baseline : public BeaconingStrategy{
public:
    /**
     * @brief Disseminates the valid beacons towards multiple interfaces of the appropriate neighbours until the limit for
     * sending beacons with equal source ASes & hop count? to one neighbour is reached.
     */
    void DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces, SCION_Node* node) override;
protected:
    /**
     * @brief Does nothing. This strategy does not evict any beacons.
     */
    void HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                               SCION_Node* node, SCION_Node* remote_as,
                               ld latency, ld bwd) override;
    /**
     * @brief Does nothing. This strategy does not use a specialized beacon store.
     */
    void UpdateSpecializedBeaconStore(SCION_Node* remote_as, ld latency, ld bwd, uint16_t src_as_no,
                                      beacon *new_beacon) override;
};
#endif //SCION_BEACONING_SIMMULATOR_BASELINE_H
