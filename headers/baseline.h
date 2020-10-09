/**
 * @file baseline.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beaconing_strategy.h
 * @brief Defines specialized functions for the baseline beaconing strategy.
 */

#ifndef SCION_BEACONING_SIMMULATOR_BASELINE_H
#define SCION_BEACONING_SIMMULATOR_BASELINE_H
#include "beaconing_strategy.h"

namespace ns3 {
class Baseline : public BeaconingStrategy
{
  public:
    /**
     * @brief Disseminates the valid beacons towards multiple interfaces of the appropriate neighbours until the limit for
     * sending beacons with equal source ASes to one neighbour is reached.
     */
    void
    DisseminateBeacons (SCION_Node::neighbour_relation relation) override;

    void DoInitializations(uint32_t all_nodes) override;

  protected:
    /**
     * @brief Does nothing. This strategy does not evict any beacons.
     */
    void ReplacementPolicy (std::string key, uint16_t src_as, beacon *old_beacon,
                            uint16_t self_egress_if_no, uint16_t remote_ingress_if_no,
                            Ptr<SCION_Node> remote_as, ld latency,
                            ld bwd) override;

    void MetaDataUpdateAfterImmediateSend (beacon *the_beacon, uint16_t local_iface, Ptr<SCION_Node> remote_as,
                                  uint16_t dst_as_no) override;
    void MetaDataUpdatePeriodic (beacon* the_beacon) override;
};
}
#endif //SCION_BEACONING_SIMMULATOR_BASELINE_H
