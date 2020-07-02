/**
 * @file criteria_matching.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Defines a specialized beaconing strategy and its associated constants which
 * uses criteria matching (related to bandwidth/latency/disjointness of paths) to choose which
 * beacons to disseminate.
 */

#ifndef SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#define SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#include "beaconing_strategy.h"
class CriteriaMatching : public BeaconingStrategy {
public:
    /**
     * @brief Disseminates highest scoring beacons towards multiple interfaces of the appropriate neighbours until the limit for
     * sending the same beacon to one neighbour is reached.
     */
    void DisseminateBeacons(const std::unordered_map<uint16_t, std::vector<uint16_t>> &valid_interfaces,
                            SCION_Node* node) override;

protected:
    /**
     * @brief Evicts the lowest scored beacon for the beacons originating AS if the score of the new beacon is larger
     * than the lowest scored matching beacon found in the remote ASes beacon store.
     */
    void HandleFullBeaconStore(std::string key, uint16_t src_as, beacon *old_beacon, uint16_t self_egress_if_no,
                               uint16_t remote_as_no, uint16_t remote_ingress_if_no,
                               SCION_Node* node, SCION_Node* remote_as,
                               ld latency, ld bwd) override;

    /**
     * @brief Updates the structure holding all the beacons sorted by score.
     */
    void UpdateSpecializedBeaconStore(SCION_Node* remote_as, ld latency, ld bwd, uint16_t src_as_no,
                                      beacon *new_beacon) override;

private:
    /**
     * @brief Calculates the beacon's score in the context of the remote AS' preferences.
     */
    static ld CalculateBeaconScore(SCION_Node* remote_as, ld latency, ld bwd);
};
#endif //SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
