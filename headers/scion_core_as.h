//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#include "scion_node.h"

class SCION_Core_As : public SCION_Node{
public:

    SCION_Core_As(uint16_t as_number, uint32_t system_id, coefficients coefs, const simulator_params &periods, BeaconingStrategy* strategy) :
    SCION_Node(as_number, system_id, coefs, periods, strategy) {}

    void CoreBeaconing() override;
    void IntraISDBeaconing() override;
    void ProcessReceivedBeacons(uint16_t src_as_no, uint16_t ingress_if, beacon* the_beacon) override;

};
#endif //SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
