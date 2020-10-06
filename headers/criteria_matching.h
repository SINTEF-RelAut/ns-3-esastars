/**
 * @file criteria_matching.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Defines a specialized beaconing strategy that uses criteria matching
 * (related to bandwidth/latency/disjointness of paths) to choose which
 * beacons to disseminate.
 */

#ifndef SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#define SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
#include "beaconing_strategy.h"

namespace ns3 {
class CriteriaMatching : public BeaconingStrategy
{
  public:
    /**
     * @brief Disseminates highest scoring beacons towards multiple interfaces of the appropriate neighbours until the limit for
     * sending beacons with the same originating source AS to one neighbour is reached.
     */
    void
    DisseminateBeacons (SCION_Node::neighbour_relation relation) override;

  protected:
    /**
     * @brief Evicts the lowest scored beacon for the beacons originating AS if the score of the new beacon is larger
     * than the lowest scored matching beacon found in the remote ASes beacon store.
     */

    void
    ReplacementPolicy (std::string key, uint16_t src_as, beacon *old_beacon,
                                 uint16_t self_egress_if_no, uint16_t remote_ingress_if_no,
                                 Ptr<SCION_Node> remote_as, ld latency,
                                 ld bwd) override;

    void
    MetaDataUpdateAfterSend (beacon *the_beacon, uint16_t local_iface, Ptr<SCION_Node> remote_as,
                                       uint16_t dst_as_no) override;
    void
    MetaDataUpdatePeriodic (beacon* the_beacon) override;

  private:


};
}
#endif //SCION_BEACONING_SIMMULATOR_CRITERIA_MATCHING_H
