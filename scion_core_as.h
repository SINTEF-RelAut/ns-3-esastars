//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#include "scion_node.h"

class SCION_Core_As : public SCION_Node{
public:

    SCION_Core_As(uint16_t as_number, uint32_t system_id, ld latency_coef, ld bandwidth_coef, ld AS_level_diversity_coef,
    ld link_level_diversity_coef, ns3::Time beaconing_period, int64_t expiration_period, BeaconingStrategy* strategy) :
    SCION_Node(as_number, system_id, latency_coef, bandwidth_coef, AS_level_diversity_coef,
            link_level_diversity_coef, beaconing_period, expiration_period, strategy) {}

    void CoreBeaconing();
    void IntraISDBeaconing() override;
    void ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon) override;

protected:
    std::unordered_map<uint16_t, std::vector<uint16_t>> select_valid_interfaces() override;
};
#endif //SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
